#!/usr/bin/env python3
"""Exercise hook CLI I/O and parent discovery with an isolated control socket."""
import json
import os
from pathlib import Path
import pty
import socket
import subprocess
import tempfile
import threading
import time

binary = str(Path('build/bongocat').resolve())
with tempfile.TemporaryDirectory(prefix='bongo-hook-client-') as directory:
    env = dict(os.environ, XDG_RUNTIME_DIR=directory)
    env.pop('BONGOCAT_HOOK_DEBUG', None)
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as server:
        server.bind(str(Path(directory) / 'bongocat.sock'))
        server.listen(1)

        def invoke(payload, expected=None, shell=False):
            requests = []
            errors = []

            def receive():
                try:
                    server.settimeout(3)
                    connection, _ = server.accept()
                    with connection:
                        connection.settimeout(3)
                        requests.append(connection.recv(64).decode())
                        connection.sendall(b'0 ok')
                except Exception as error:
                    errors.append(str(error))

            worker = threading.Thread(target=receive) if expected else None
            if worker:
                worker.start()
            args = [binary, '--hook', 'claude']
            if shell:
                # The trailing builtin prevents the shell from exec-ing the hook.
                args = ['/bin/sh', '-c', '"$1" --hook claude; :', 'probe', binary]
            result = subprocess.run(args, input=payload, env=env,
                                    capture_output=True, timeout=4)
            if worker:
                worker.join(timeout=4)
                assert not worker.is_alive() and not errors, errors
                assert requests == [expected], requests
            else:
                server.settimeout(0)
                try:
                    connection, _ = server.accept()
                except BlockingIOError:
                    pass
                else:
                    connection.close()
                    raise AssertionError('Ignored payload sent a request')
            assert result.returncode == 0, result.stderr
            assert result.stdout == b'' and result.stderr == b'', result

        event = {'hook_event_name': 'PreToolUse', 'session_id': 'test'}
        expected = f'ev claude working e430d22bdbbe8583 {os.getpid()}'
        invoke(json.dumps(event).encode(), expected)
        invoke(json.dumps(event).encode(), expected, shell=True)
        event['tool_input'] = {'content': 'x' * (4 * 1024 * 1024)}
        invoke(json.dumps(event).encode(), expected)
        for payload in (b'', b'{', b'{}', b'{"hook_event_name":"Unknown"}',
                        b'{"hook_event_name":"Stop","stop_hook_active":true}',
                        b'{"nested":{"hook_event_name":"Stop"}}',
                        b'{"hook_event_name":"Stop"} garbage'):
            invoke(payload)

        # A producer holding stdin open must not stall the agent indefinitely.
        start = time.monotonic()
        process = subprocess.Popen([binary, '--hook', 'claude'], env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        try:
            assert process.wait(timeout=3) == 0
            assert 1.5 <= time.monotonic() - start < 3
            assert process.stdout.read() == b'' and process.stderr.read() == b''
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdin.close()
            process.stdout.close()
            process.stderr.close()

        master, slave = pty.openpty()
        try:
            result = subprocess.run([binary, '--hook', 'claude'], stdin=slave,
                                    env=env, capture_output=True, timeout=1)
            assert result.returncode == 0 and not result.stdout and not result.stderr
        finally:
            os.close(master)
            os.close(slave)

    # No overlay must also be quiet and successful.
    Path(directory, 'bongocat.sock').unlink()
    result = subprocess.run([binary, '--hook', 'claude'], env=env,
                            input=b'{"hook_event_name":"Stop"}',
                            capture_output=True, timeout=3)
    assert result.returncode == 0 and not result.stdout and not result.stderr
    for args in (['--hook'], ['--hook', 'Claude'], ['--hook', 'toolongname'],
                 ['--hook', 'claude', '--state', 'done'],
                 ['--sessions', '--hook', 'claude']):
        result = subprocess.run([binary, *args], input=b'', env=env,
                                capture_output=True, timeout=3)
        assert result.returncode == 1 and not result.stdout, (args, result)
print('Hook streaming, quiet I/O, parent discovery, timeout and CLI checks passed.')
