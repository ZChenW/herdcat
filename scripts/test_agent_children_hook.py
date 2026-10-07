#!/usr/bin/env python3
"""A headless codex exec hook must transmit its real candidate PID quietly."""
import ctypes
import json
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from runtime_test_helpers import runtime_env

BINARY = str(Path('build/herdcat').resolve())


def child():
    # Name this fake agent process, detach its controlling tty, run the hook.
    ctypes.CDLL(None).prctl(15, b'codex', 0, 0, 0)
    payload = {'hook_event_name': 'PreToolUse', 'session_id': 'agent-child'}
    return subprocess.run([BINARY, '--hook', 'codex'], input=json.dumps(payload),
                          text=True, capture_output=True, timeout=4)


def run():
    with tempfile.TemporaryDirectory(prefix='herdcat-child-hook-') as directory:
        env = runtime_env(XDG_RUNTIME_DIR=directory)
        env.pop('HERDCAT_HOOK_DEBUG', None)
        requests, errors = [], []
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as server:
            server.bind(str(Path(directory) / 'herdcat.sock'))
            server.listen(1)
            server.settimeout(4)

            def receive():
                try:
                    connection, _ = server.accept()
                    with connection:
                        requests.append(connection.recv(1280).decode())
                        connection.sendall(b'0 ok')
                except Exception as error:
                    errors.append(str(error))

            worker = threading.Thread(target=receive)
            worker.start()
            fake = subprocess.Popen([sys.executable, __file__, '--child'], env=env,
                                    start_new_session=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)
            out, err = fake.communicate(timeout=6)
            worker.join(timeout=5)
            assert fake.returncode == 0 and not out and not err, (out, err)
            assert not worker.is_alive() and not errors, errors
            assert len(requests) == 1, requests
            fields = requests[0].split()
            assert fields[:3] == ['ev', 'codex', 'working'], fields
            assert fields[4:] == ['0', str(fake.pid), '0'], fields
        print('Agent children headless hook candidate handoff passed.')


if __name__ == '__main__':
    if '--child' in sys.argv:
        result = child()
        assert result.returncode == 0 and not result.stdout and not result.stderr, result
    else:
        run()
