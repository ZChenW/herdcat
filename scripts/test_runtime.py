#!/usr/bin/env python3
"""Run isolated runtime regressions against the protocol fixture."""
import os
from pathlib import Path
import signal
import socket
import threading
import subprocess
import tempfile
import time

binary = str(Path("build/herdcat").resolve())
fixture = str(Path("build/compositor/server").resolve())


def wait_for(condition, seconds=4):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.03)
    raise AssertionError("condition did not become true")


with tempfile.TemporaryDirectory(prefix="herdcat-integration-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
               WAYLAND_DISPLAY="wayland-test")
    config = root / "cat.conf"
    config.write_text("monitor=TEST-1,TEST-2\noverlay_opacity=0\nfps=1\n"
                      "sign_done=timeout\nagent_done_timeout=1\ntest_animation_interval=1\n[monitor:TEST-2]\ncat_height=60\n"
                      "[global]\ncat_height=40\n")
    compositor_log = (root / "compositor.log").open("w+")
    app_log = (root / "app.log").open("w+")
    server = subprocess.Popen([fixture], env=env, stdout=compositor_log,
                              stderr=compositor_log)
    app = None

    def command(name, success=True):
        result = subprocess.run([binary, "--" + name], env=env,
                                capture_output=True, text=True, timeout=3)
        assert (result.returncode == 0) == success, (name, result.stdout, result.stderr)
        return result.stdout

    def wire(request, success=True):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
            control.settimeout(2)
            control.connect(str(root / "herdcat.sock"))
            control.sendall(request.encode() if isinstance(request, str) else request)
            response = control.recv(512).decode()
        assert response.startswith("0 ") == success, (request, response)
        return response

    try:
        wait_for(lambda: (root / "wayland-test").exists())
        app = subprocess.Popen([binary, "-c", str(config), "-w"], env=env,
                               stdout=app_log, stderr=app_log)
        wait_for(lambda: (root / "herdcat.sock").exists())
        assert "paused=no" in command("status")
        assert app.poll() is None
        competing = subprocess.run([binary, "-c", str(config)], env=env,
                                   capture_output=True, timeout=3)
        assert competing.returncode != 0
        for name in ("hide", "show", "pause"):
            command(name)
        assert "paused=yes" in command("status")
        command("resume")
        assert "paused=no" in command("status")
        # Exercise the real daemon parser, not just the transport callback.
        assert "No agent sessions" in command("sessions")
        for request in (
            "ev TOOL working 0123456789abcdef 0",
            "ev opencodeworking 0123456789abcdef 0",
            "ev claude working 0123456789abcdef0 0",
            "ev claude working 0000000000000000 0",
            "ev claude working 0123456789abcdef -1",
            "ev claude working 0123456789abcdef 4194305",
            "ev claude working 0123456789abcdef 42 garbage",
        ):
            wire(request, success=False)
        wire("ev claude waiting aaaaaaaaaaaaaaaa 0")
        wire("ev codex working bbbbbbbbbbbbbbbb 0")
        wire("ev codex working bbbbbbbbbbbbbbbb 0")
        wire("name aaaaaaaaaaaaaaaa 项目 with spaces")
        assert "项目 with spaces" in command("sessions")
        wire("name eeeeeeeeeeeeeeee unknown")
        for request in ("name aaaaaaaaaaaaaaaa ", "name aaaaaaaaaaaaaaa name",
                        "name aaaaaaaaaaaaaaaag name", "name aaaaaaaaaaaaaaaa " + "x" * 42,
                        b"name aaaaaaaaaaaaaaaa bad\xff", "name aaaaaaaaaaaaaaaa bad\nname"):
            wire(request, success=False)
        assert "agent=waiting sessions=2" in command("status")
        wire("ev claude done aaaaaaaaaaaaaaaa 0")
        assert "agent=done" in command("status")
        command("pause")
        wait_for(lambda: "agent=working" in command("status"))
        command("resume")
        wire("state waiting")
        assert "sessions=3" in command("status")
        wire("state idle")
        assert "sessions=2" in command("status")
        wire("ev claude end aaaaaaaaaaaaaaaa 0")
        wire("ev codex end bbbbbbbbbbbbbbbb 0")
        assert "No agent sessions" in command("sessions")
        # Both logical sessions share one process watch and die together.
        agent = subprocess.Popen(["sleep", "30"])
        try:
            wire(f"ev claude waiting cccccccccccccccc {agent.pid}")
            wire(f"ev codex working dddddddddddddddd {agent.pid}")
            command("reload")
            assert "sessions=2" in command("status")
            agent.terminate()
            agent.wait(timeout=3)
            wait_for(lambda: "sessions=0" in command("status"))
        finally:
            if agent.poll() is None:
                agent.terminate()
                agent.wait(timeout=3)
        # Invalid, missing, and unreadable-as-config reloads are transactional.
        config.write_text("fps=invalid\n")
        command("reload", success=False)
        assert app.poll() is None
        config.unlink()
        command("reload", success=False)
        replacement = root / "replacement"
        replacement.write_text("monitor=TEST-1,TEST-2\ncat_height=45\nfps=60\n")
        replacement.replace(config)
        command("reload")
        # Kill only the input helper owned by this test instance.
        children_path = Path(f"/proc/{app.pid}/task/{app.pid}/children")
        wait_for(lambda: children_path.read_text().strip())
        helper = int(children_path.read_text().split()[0])
        os.kill(helper, signal.SIGKILL)
        wait_for(lambda: children_path.read_text().strip() and
                 int(children_path.read_text().split()[0]) != helper, seconds=7)
        # Fixture: resolution/scale change, unplug/replug, output queue pressure.
        time.sleep(3)
        assert server.poll() is None and app.poll() is None
        command("status")
        # Stop server reads and fill the renderer's outgoing Wayland socket.
        # Controls must continue responding while flush waits for POLLOUT.
        os.kill(server.pid, signal.SIGSTOP)
        resume_server = threading.Timer(2, lambda: os.kill(server.pid, signal.SIGCONT))
        resume_server.start()
        try:
            for _ in range(2500):
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                    control.settimeout(1.5)
                    control.connect(str(root / "herdcat.sock"))
                    control.sendall(b"reload")
                    assert control.recv(512).startswith(b"0 ")
        finally:
            os.kill(server.pid, signal.SIGCONT)
            resume_server.cancel()
        time.sleep(0.2)
        assert app.poll() is None
        # Reconcile output list and wait for an explicitly missing monitor.
        config.write_text("monitor=MISSING\nfps=1\n")
        command("reload")
        assert app.poll() is None
        config.write_text("monitor=TEST-1\nfps=1\ncat_x_offset=-2147483648\n"
                          "cat_y_offset=2147483647\n")
        command("reload")
        # Direct signals must exit cleanly, preserve the lock inode, and reap helper.
        inode = (root / "herdcat.pid").stat().st_ino
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGQUIT, signal.SIGHUP):
            app.send_signal(sig)
            assert app.wait(timeout=3) == 0
            assert (root / "herdcat.pid").stat().st_ino == inode
            assert not (root / "herdcat.sock").exists()
            assert not Path(f"/proc/{helper}").exists()
            app = subprocess.Popen([binary, "-c", str(config)], env=env,
                                   stdout=app_log, stderr=app_log)
            wait_for(lambda: (root / "herdcat.sock").exists())
            wait_for(lambda: Path(f"/proc/{app.pid}/task/{app.pid}/children").read_text().strip())
            helper = int(Path(f"/proc/{app.pid}/task/{app.pid}/children").read_text().split()[0])
        command("stop", success=False)  # Public stop option is intentionally absent.
        subprocess.run([binary, "--toggle"], env=env, check=True, timeout=3)
        assert app.wait(timeout=3) == 0
        compositor_log.flush()
        text = (root / "compositor.log").read_text()
        assert "overlay TEST-1" in text and "overlay TEST-2" in text
        assert "phase 4" in text and "commit TEST-1" in text
        # A transparent overlay is only as wide as the cat and its signs, at
        # each output's scale. The reloaded config has the default visible
        # bar, which still spans the output.
        assert "TEST-1 300x145" in text and "TEST-1 357x174" in text
        assert "TEST-2 712x304" in text
        assert "TEST-1 960x186" in text and "TEST-2 2048x248" in text
        assert "visible TEST-1 0" in text and "visible TEST-2 0" in text
        assert "visible TEST-1 1" in text
        assert text.count("overlay TEST-2") >= 2
        print("Runtime controls, helper restart, signals, reloads, output lifecycle and buffer tests passed.")
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
            print((root / "app.log").read_text())
        if server.returncode or (app and app.returncode):
            print((root / "compositor.log").read_text())
        app_log.close()
        compositor_log.close()
