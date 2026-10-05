#!/usr/bin/env python3
"""v2 envelope replay through the bridge and real hook client, no live service."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile

binary = Path('build/bongocat').resolve()
with tempfile.TemporaryDirectory(prefix='bongo-opencode-') as directory:
    root = Path(directory)
    (root / 'bongocat').symlink_to(binary)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory,
               PATH=directory + os.pathsep + os.environ['PATH'])
    env.pop('BONGOCAT_HOOK_DEBUG', None)
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as server:
        server.bind(str(root / 'bongocat.sock'))
        server.listen(16)
        server.settimeout(4)
        proc = subprocess.Popen(['node', 'tests/integrations/opencode_driver.mjs'],
                                env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        try:
            assert proc.stdout.readline().strip() == 'ready'
            def send(name, sid='bridge-test', info=None):
                event = {'type': name, 'data': {'sessionID': sid,
                         'content': 'DO_NOT_TRANSMIT'}}
                proc.stdin.write(json.dumps(dict(event=event, info=info)) + '\n')
                proc.stdin.flush()
            def receive():
                connection, _ = server.accept()
                with connection:
                    request = connection.recv(64).decode()
                    connection.sendall(b'0 ok')
                    return request
            send('session.created', info={'location': {'directory': '/tmp/bridge-test'}})
            start = receive().split()
            assert start[:3] == ['ev', 'opencode', 'start'] and start[-1] == '0', start
            key = start[3]
            assert receive() == f'name {key} bridge-test'
            # No location on these events. Cached metadata and ordering survive.
            for event, action in [('session.execution.started', 'working'),
                                  ('permission.asked', 'waiting'),
                                  ('permission.replied', 'working'),
                                  ('session.execution.interrupted', 'interrupt'),
                                  ('session.execution.succeeded', 'done')]:
                send(event)
                assert receive() == f'ev opencode {action} {key} 0'
                if event == 'session.execution.started':
                    assert receive() == f'name {key} bridge-test'
            send('session.created', 'child', {'location': {'directory': '/tmp/bridge-test'},
                                             'parentID': 'parent'})
            send('session.execution.succeeded', 'child')
            send('session.created', 'foreign', {'location': {'directory': '/tmp/other'}})
            send('session.execution.started', 'unavailable')
            send('session.step.failed')
            send('session.deleted')
            assert receive() == f'ev opencode end {key} 0'
            proc.stdin.close()
            assert proc.wait(3) == 0 and proc.stderr.read() == ''
            assert proc.stdout.read() == ''
            # Even a malicious supplied PID is ignored for the service adapter.
            result_proc = subprocess.Popen([str(binary), '--hook', 'opencode', '--event',
                                            'session.execution.succeeded'], env=env,
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE)
            result_proc.stdin.write(json.dumps({'agent_pid': os.getpid(),
                                                'session_id': 'claimed'}).encode())
            result_proc.stdin.close()
            assert receive().endswith(' 0')
            assert result_proc.wait(3) == 0 and result_proc.stdout.read() == b''
            assert result_proc.stderr.read() == b''
            server.settimeout(0.1)
            try:
                server.accept()
            except TimeoutError:
                pass
            else:
                raise AssertionError('Ignored event produced a request')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
print('opencode v2 bridge state, metadata, child, no-PID and privacy checks passed.')
