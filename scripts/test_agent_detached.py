#!/usr/bin/env python3
"""Session recovery and pointer handling detached ownership over the production parser/pidfd fixture."""
from pathlib import Path
import os
import ctypes
import json
import sys
import threading
import socket
import subprocess
import tempfile

fixture = str(Path('build/agent_children_fixture').resolve())


def fixture_test():
    with tempfile.TemporaryDirectory(prefix='herdcat-agent-detached-') as temporary:
        root = Path(temporary)
        parent = subprocess.Popen(['sleep', '60'])
        child = subprocess.Popen(['sleep', '60'], start_new_session=True,
                                 env=dict(os.environ, CLAUDE_PID=str(parent.pid)))
        app = None
        try:
            # Model complete reparenting without modifying any host /proc entry.
            for process in (parent, child):
                directory = root / str(process.pid)
                directory.mkdir()
                (directory / 'stat').write_text(f'{process.pid} (agent) S 1 1 1 0 0\n')
            client, server = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            with client, server:
                app = subprocess.Popen([fixture, temporary, str(server.fileno())],
                                       pass_fds=(server.fileno(),))
                server.close()
                client.settimeout(3)
                def wire(message):
                    client.sendall(message.encode())
                    return client.recv(4096).decode()
                assert wire(f'ev claude start 1111111100000000 {parent.pid}') == 'ok'
                assert wire(f'ev codex working 2222222200000000 0 {child.pid} 1 {parent.pid}') == 'ok'
                assert 'parent=11111111' in wire('sessions')
                assert wire('labels') == 'Claude + Codex | Claude +1\n'
                assert wire('ev codex end 2222222200000000 0') == 'ok'
                for owner in (child.pid, 4194304):
                    assert wire(f'ev codex start 2222222200000000 0 {child.pid} 1 {owner}') == 'ok'
                    assert 'parent=' not in wire('sessions')
                    assert wire('ev codex end 2222222200000000 0') == 'ok'
                assert wire('ev claude end 1111111100000000 0') == 'ok'
                assert wire(f'ev kimi start 1111111100000000 {parent.pid}') == 'ok'
                assert wire(f'ev codex start 2222222200000000 0 {child.pid} 1 {parent.pid}') == 'ok'
                assert 'parent=' not in wire('sessions')
                client.sendall(b'stop')
                assert app.wait(timeout=3) == 0
                print('Session recovery and pointer handling detached owner validation and labels passed.')
        finally:
            for process in (app, child, parent):
                if process and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=3)


def hook_test():
    binary = str(Path('build/herdcat').resolve())
    with tempfile.TemporaryDirectory(prefix='herdcat-agent-owner-hook-') as temporary:
        env = dict(os.environ, XDG_RUNTIME_DIR=temporary, CLAUDE_PID=str(os.getpid()))
        env.pop('HERDCAT_HOOK_DEBUG', None)
        received, errors = [], []
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as listener:
            listener.bind(str(Path(temporary) / 'herdcat.sock'))
            listener.listen(1)
            listener.settimeout(4)
            def receive():
                try:
                    client, _ = listener.accept()
                    with client:
                        received.append(client.recv(1280).decode())
                        client.sendall(b'0 ok')
                except Exception as error:
                    errors.append(str(error))
            worker = threading.Thread(target=receive)
            worker.start()
            child = subprocess.Popen([sys.executable, __file__, '--hook-child'],
                                     env=env, start_new_session=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True)
            out, err = child.communicate(timeout=6)
            worker.join(timeout=5)
            assert child.returncode == 0 and not out and not err
            assert not worker.is_alive() and not errors and len(received) == 1
            fields = received[0].split()
            assert fields[:3] == ['ev', 'codex', 'working']
            assert fields[4:] == ['0', str(child.pid), '0', str(os.getpid())]
        print('Session recovery and pointer handling headless hook inherited owner handoff passed.')


if __name__ == '__main__':
    if '--hook-child' in sys.argv:
        ctypes.CDLL(None).prctl(15, b'codex', 0, 0, 0)
        binary = str(Path('build/herdcat').resolve())
        payload = dict(hook_event_name='PreToolUse', session_id='detached-child')
        result = subprocess.run([binary, '--hook', 'codex'], input=json.dumps(payload),
                                capture_output=True, text=True, timeout=4)
        assert result.returncode == 0 and not result.stdout and not result.stderr
    else:
        fixture_test()
        hook_test()
