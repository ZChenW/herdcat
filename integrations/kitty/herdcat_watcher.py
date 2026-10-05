"""Report the focused kitty split. This must not block kitty or print.

The payload is the control-protocol command ``pane <pid> <split>`` with no
newline: the same text ``herdcat --pane`` sends from src/core/main.c.
The socket path follows src/core/control.c path() with the suffix sock.
If either of those changes, update this file with it.
"""

import os
import socket
import stat

# Linux sockaddr_un.sun_path is 108 bytes, including the terminating NUL.
_SUN_PATH = 108
_TIMEOUT_S = 0.03


def control_socket_path():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if runtime:
        try:
            info = os.stat(runtime)
        except OSError:
            return None
        if (
            not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.getuid()
            or (info.st_mode & 0o022)
        ):
            return None
        path = "%s/herdcat.sock" % runtime
    else:
        path = "/tmp/herdcat-%d.sock" % os.getuid()
    if len(path) >= _SUN_PATH:
        return None
    return path


def send_pane(pid, split):
    path = control_socket_path()
    if not path:
        return
    payload = ("pane %d %d" % (pid, split)).encode("ascii")
    sock = None
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        sock.settimeout(_TIMEOUT_S)
        sock.connect(path)
        sock.send(payload)
    except Exception:
        return
    finally:
        if sock is not None:
            try:
                sock.close()
            except Exception:
                pass


def on_focus_change(boss, window, data):
    del boss
    try:
        if not isinstance(data, dict) or data.get("focused") is not True:
            return
        ident = getattr(window, "id", None)
        if type(ident) is not int or ident <= 0:
            return
        pid = os.getpid()
        if pid <= 1:
            return
        send_pane(pid, ident)
    except Exception:
        return
