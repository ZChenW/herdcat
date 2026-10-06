#!/usr/bin/env python3
"""Run the real bridge and hook client against an isolated control socket."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile

from runtime_test_helpers import run_on_pty, runtime_env

run_on_pty()

binary = Path('build/herdcat').resolve()
with tempfile.TemporaryDirectory(prefix='bongo-pi-') as directory:
    root = Path(directory)
    (root / 'herdcat').symlink_to(binary)
    env = runtime_env(XDG_RUNTIME_DIR=directory,
               PATH=directory + os.pathsep + os.environ['PATH'])
    env.pop('HERDCAT_HOOK_DEBUG', None)
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as server:
        server.bind(str(root / 'herdcat.sock'))
        server.listen(16)
        server.settimeout(4)
        proc = subprocess.Popen(['node', 'tests/integrations/pi_driver.mjs'],
                                env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        try:
            pid = int(proc.stdout.readline())
            def send(name, event=None, child=False):
                proc.stdin.write(json.dumps(dict(name=name, event=event or {}, child=child)) + '\n')
                proc.stdin.flush()
            def receive():
                connection, _ = server.accept()
                with connection:
                    request = connection.recv(1280).decode()
                    connection.sendall(b'0 ok')
                    return request
            send('session_start')
            start = receive().split()
            assert start[:3] == ['ev', 'pi', 'start'] and int(start[-1]) == pid, start
            key = start[3]
            assert receive() == f'sid {key} bridge-test'
            assert receive() == f'name {key} bridge-test'
            # A burst remains ordered; assistant error text never enters the wire.
            send('before_agent_start', {'prompt': 'DO_NOT_TRANSMIT'})
            send('tool_call', {'args': 'DO_NOT_TRANSMIT'})
            send('agent_end', {'messages': [{'role': 'assistant', 'stopReason': 'error',
                                           'errorMessage': 'DO_NOT_TRANSMIT'}]})
            assert receive() == f'ev pi working {key} {pid}'
            assert receive() == f'sid {key} bridge-test'
            assert receive() == f'name {key} bridge-test'
            assert receive() == f'ev pi working {key} {pid}'
            assert receive() == f'ev pi fail {key} {pid}'
            send('agent_end', {'messages': [{'role': 'assistant', 'stopReason': 'stop'}]})
            assert receive() == f'ev pi done {key} {pid}'
            assert receive() == f'sid {key} bridge-test'
            assert receive() == f'name {key} bridge-test'
            send('agent_start', child=True)
            send('session_shutdown')
            assert receive() == f'ev pi end {key} {pid}'
            proc.stdin.close()
            assert proc.wait(3) == 0 and proc.stderr.read() == ''
            assert proc.stdout.read() == ''
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        # Invalid claimed PIDs cannot bind another process or create a session.
        for pid_value in [1, os.getpid() + 1000000, 'wrong', -1]:
            payload = json.dumps({'agent_pid': pid_value, 'session_id': 'invalid'})
            result = subprocess.run([str(binary), '--hook', 'pi', '--event', 'agent_start'],
                                    input=payload, env=env, capture_output=True, text=True)
            assert result.returncode == 0 and not result.stdout and not result.stderr
        server.settimeout(0.1)
        try:
            server.accept()
        except TimeoutError:
            pass
        else:
            raise AssertionError('Invalid PID or child event reached daemon')
    # Missing herdcat must never reject an agent callback or write errors.
    missing_env = dict(env, PATH=directory + '/absent')
    result = subprocess.run(['/usr/bin/node', 'tests/integrations/pi_driver.mjs'],
                            env=missing_env, input='{"name":"agent_start"}\n',
                            capture_output=True, text=True, timeout=3)
    assert result.returncode == 0 and result.stderr == '', result
print('Pi bridge ordering, PID, status, child filtering, privacy and failure checks passed.')
