#!/usr/bin/env python3
"""Outside-sandbox acceptance: installed setgid helper + real socketpair.

Build with `make input-helper-runtime-build INPUT_HELPER_PATH=/installed/path`.
Run as a user WITHOUT input in primary/supplementary groups, with readable
keyboards only through the installed root:input helper. No device writes,
setgid file creation, or keyboard grabs are performed here.
"""
import argparse
import grp
import os
from pathlib import Path
import re
import select
import socket
import tempfile
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("--helper", default="/usr/local/lib/herdcat/herdcat-input")
parser.add_argument("--renderer-status", action="store_true",
                    help="also test --status through the isolated compositor fixture")
parser.add_argument("--expect-keyboard", action="store_true")
parser.add_argument("--expect-paw", action="store_true",
                    help="type a key during the 10-second observation window")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
input_gid = grp.getgrnam("input").gr_gid
assert input_gid not in [os.getgid(), *os.getgroups()], "use a user outside input"


def line(process, timeout=4):
    assert select.select([process.stdout], [], [], timeout)[0], "fixture timeout"
    text = process.stdout.readline().decode().strip()
    assert text, "fixture exited before report"
    return text


def stopped(pid):
    try:
        return Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1][0] == "Z"
    except FileNotFoundError:
        return True


for fixture, mode in (("input_helper_fixture", "standalone"),
                      ("input_fallback_fixture", "in-process")):
    listing = subprocess.run([str(root / "build" / fixture), "--list-devices"],
                             capture_output=True, timeout=5)
    assert f"input-helper={mode}" in listing.stdout.decode()
    if mode == "in-process":
        assert "missing or not executable" in listing.stdout.decode()
    for interval in ("--once", "--hotplug"):
        process = subprocess.Popen([str(root / "build" / fixture), interval],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, bufsize=0)
        child = None
        try:
            header = line(process)
            assert f"input-helper={mode}" in header
            child = int(re.search(r"helper-pid=(\d+)", header)[1])
            hint = line(process)
            if mode == "standalone":
                assert "setgid helper installed" in hint
                initial = line(process)
                devices = int(re.search(r"devices=(\d+)", initial)[1])
                if args.expect_keyboard:
                    assert devices > 0, "no keyboard opened with helper's group"
                status = Path(f"/proc/{child}/status").read_text()
                gids = list(map(int, next(row for row in status.splitlines()
                                         if row.startswith("Gid:")).split()[1:]))
                assert gids[0] == gids[1] == gids[3] == os.getgid()
                assert gids[2] == (os.getgid() if interval == "--once" else input_gid)
                assert "NoNewPrivs:\t1" in status and "Seccomp:\t2" in status
                if args.expect_paw and interval == "--hotplug":
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        if re.search(r"paws=[123](?:$|\s)", line(process)):
                            break
                    else:
                        raise AssertionError("no paw packet observed")
            process.kill()  # parent abruptly dies, including while helper idle
            process.wait(timeout=3)
            deadline = time.monotonic() + 3
            while not stopped(child) and time.monotonic() < deadline:
                time.sleep(0.05)
            assert stopped(child), "helper survived loss of parent socket"
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=3)
            if child and not stopped(child):
                os.kill(child, 15)
print("installed setgid access, fallback/list diagnostics and parent death passed")

# Invalid socket types must be rejected before device access.
left, right = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    result = subprocess.run([args.helper, str(right.fileno()), "0", "1", "0"],
                            pass_fds=(right.fileno(),), capture_output=True,
                            timeout=3)
    assert result.returncode == 1 and not result.stdout and not result.stderr
finally:
    left.close()
    right.close()

if args.renderer_status:
    from runtime_test_helpers import runtime_env, wait_until
    with tempfile.TemporaryDirectory(prefix="herdcat-input-status-") as directory:
        runtime = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY="wayland-test")
        config = runtime / "cat.conf"
        config.write_text("monitor=TEST-1\nhotplug_scan_interval=1\nfps=1\n")
        app = None
        with (runtime / "compositor.log").open("w") as server_log, \
                (runtime / "renderer.log").open("w") as app_log:
            server = subprocess.Popen([str(root / "build/compositor/server")],
                                      env=env, stdout=server_log, stderr=server_log)
            try:
                wait_until(lambda: (runtime / "wayland-test").exists(), 4)
                app = subprocess.Popen([str(root / "build/herdcat"), "-c", str(config)],
                                       env=env, stdout=app_log, stderr=app_log)
                wait_until(lambda: (runtime / "herdcat.sock").exists(), 4)
                status = subprocess.run([str(root / "build/herdcat"), "--status"],
                                        env=env, capture_output=True, text=True,
                                        timeout=3)
                assert status.returncode == 0, status.stderr
                assert "input-helper=standalone" in status.stdout, status.stdout
                assert "setgid helper installed" in status.stdout, status.stdout
            finally:
                if app and app.poll() is None:
                    app.terminate()
                    app.wait(timeout=4)
                server.terminate()
                server.wait(timeout=4)
    print("renderer --status reports standalone mode through control socket")
