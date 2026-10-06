#!/usr/bin/env python3
"""Replay synthetic hooks and appended JSONL through an isolated real daemon."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

binary = str(Path("build/herdcat").resolve())
fixture = str(Path("build/compositor/server").resolve())
marker = {"type": "user", "message": {"role": "user", "content": [
    {"type": "text", "text": "[Request interrupted by user]"}]}}


def wait_for(predicate):
    until = time.monotonic() + 3
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("runtime condition timed out")


with tempfile.TemporaryDirectory(prefix="bongo-transcript-runtime-") as directory:
    root = Path(directory)
    env = dict(os.environ, HOME=directory, XDG_RUNTIME_DIR=directory,
               XDG_STATE_HOME=directory, XDG_CONFIG_HOME=directory,
               WAYLAND_DISPLAY="wayland-test", XDG_CURRENT_DESKTOP="test")
    env.pop("NIRI_SOCKET", None)
    env.pop("HERDCAT_HOOK_DEBUG", None)
    config = root / "cat.conf"
    base = "monitor=TEST-1\nfps=1\nhotplug_scan_interval=0\nagent_stale_timeout=0\n"
    config.write_text(base)
    # Longer than the old transport buffer; spaces and Unicode survive wire I/O.
    nested = root / ("a" * 150)
    nested.mkdir()
    transcript = nested / "session with 空格.jsonl"
    transcript.write_text(json.dumps(marker) + "\n")
    log = (root / "runtime.log").open("w+")
    server = subprocess.Popen([fixture], env=env, stdout=log, stderr=log)
    app = None

    def wire(request):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
            client.settimeout(2)
            client.connect(str(root / "herdcat.sock"))
            client.sendall(request.encode())
            reply = client.recv(4096).decode()
        assert reply.startswith("0 "), reply
        return reply

    def hook(agent, event):
        payload = {"hook_event_name": event, "session_id": "transcript-test",
                   "cwd": "/test/project", "transcript_path": str(transcript)}
        result = subprocess.run([binary, "--hook", agent], input=json.dumps(payload),
                                text=True, capture_output=True, env=env, timeout=3)
        assert result.returncode == 0 and not result.stdout and not result.stderr, result

    def state(value):
        return f"agent={value}" in wire("status")

    def append(value):
        with transcript.open("a") as stream:
            stream.write(json.dumps(value) + "\n")

    try:
        wait_for(lambda: (root / "wayland-test").exists())
        app = subprocess.Popen([binary, "-c", str(config), "-w"], env=env,
                               stdout=log, stderr=log)
        wait_for(lambda: (root / "herdcat.sock").exists())
        hook("claude", "SessionStart")
        hook("claude", "UserPromptSubmit")
        assert state("working")
        # A just-submitted literal marker is ignored and never replayed later.
        append(marker)
        time.sleep(0.15)
        assert state("working")
        time.sleep(1)
        append(marker)
        wait_for(lambda: state("idle"))
        hook("claude", "UserPromptSubmit")
        hook("claude", "Stop")
        assert state("done")
        append(marker)
        assert state("done")
        hook("claude", "SessionEnd")
        hook("codex", "UserPromptSubmit")
        append({"type": "event_msg", "payload": {"type": "task_complete"}})
        time.sleep(0.1)
        assert state("working")
        append({"type": "event_msg", "payload": {"type": "task_complete",
                "error": {"codex_error_info": "usage_limit_exceeded", "message": "PRIVATE-SENTINEL"}}})
        wait_for(lambda: state("error"))
        hook("codex", "UserPromptSubmit")
        hook("codex", "Interrupt")  # All 9 characters must reach main's parser.
        assert state("idle")
        append({"type": "event_msg", "payload": {"type": "turn_aborted"}})
        assert state("idle")
        hook("codex", "UserPromptSubmit")
        append({"type": "event_msg", "payload": {"type": "turn_aborted"}})
        wait_for(lambda: state("idle"))
        hook("codex", "Interrupt")
        assert state("idle")
        hook("codex", "Stop")
        hook("codex", "Interrupt")
        assert state("done")
        for agent in ("grok", "kimi", "cursor", "copilot", "pi", "opencode"):
            wire(f"ev {agent} working 1111111111111111 0")
            wire(f"ev {agent} interrupt 1111111111111111 0")
            assert "working" not in wire("sessions")
            wire(f"ev {agent} end 1111111111111111 0")
        hook("codex", "UserPromptSubmit")
        config.write_text(base + "agent_interrupt_detect=0\n")
        wire("reload")
        append({"type": "event_msg", "payload": {"type": "turn_aborted"}})
        time.sleep(0.1)
        assert state("working")
        config.write_text(base + "agent_interrupt_detect=1\n")
        wire("reload")
        assert state("working")
        append({"type": "event_msg", "payload": {"type": "turn_aborted"}})
        wait_for(lambda: state("idle"))
        hook("codex", "SessionEnd")
        assert "No agent sessions" in wire("sessions")
        append(marker)
        wire("stop")
        assert app.wait(timeout=3) == 0
        log.flush()
        output = (root / "runtime.log").read_text()
        assert "PRIVATE-SENTINEL" not in output
        assert "AddressSanitizer" not in output and "runtime error:" not in output
        print("Transcript runtime: hooks, long paths, guard, interrupts, errors, reload and cleanup passed.")
    finally:
        if app and app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=3)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
        server.terminate()
        server.wait(timeout=3)
        if app and app.returncode:
            log.flush()
            print((root / "runtime.log").read_text())
        log.close()
