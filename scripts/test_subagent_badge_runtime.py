#!/usr/bin/env python3
"""Socket/pidfd integration for subagent display state and count badges.

Uses the production session model and sign geometry, injected ancestry and
real pidfds. Requires Unix sockets; not runnable in the restricted sandbox.
Raster pixels, layer order and reflection are covered by test_subagent_badge.
"""
from pathlib import Path
import socket
import subprocess
import tempfile

from runtime_test_helpers import wait_until

FIXTURE = str(Path('build/agent_children_fixture').resolve())
PARENT = 0x1111111100000000


def run():
    with tempfile.TemporaryDirectory(prefix='herdcat-subagent-runtime-') as directory:
        root = Path(directory)
        workers = [subprocess.Popen(['sleep', '60']) for _ in range(3)]
        app = None
        try:
            for index, worker in enumerate(workers):
                path = root / str(worker.pid)
                path.mkdir()
                ppid = workers[0].pid if index else 1
                (path / 'stat').write_text(f'{worker.pid} (agent) S {ppid} 1 1 0 0\n')
            client, server = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            with client, server:
                app = subprocess.Popen([FIXTURE, str(root), str(server.fileno())],
                                       pass_fds=(server.fileno(),))
                server.close()
                client.settimeout(3)

                def request(message):
                    client.sendall(message.encode())
                    return client.recv(4096).decode()

                def event(agent, state, key, pid):
                    assert request(f'ev {agent} {state} {key:016x} {pid}') == 'ok'

                event('claude', 'start', PARENT, workers[0].pid)
                assert 'hits=0 badges=0' in request('signs fan')
                for i, state in enumerate(('working', 'waiting'), 1):
                    event('codex', state, PARENT + i, workers[i].pid)
                fan = request('signs fan')
                assert 'hits=1 badges=1 children=2 real=idle display=working' in fan, fan
                assert 'Waiting on subagent 0 min' in fan, fan
                post = request('signs post')
                assert 'hits=1 badges=0 children=0 real=idle display=working' in post, post
                assert 'Claude +2 · Waiting on subagent 0 min' in post, post
                assert request('state') == 'idle'
                rows = request('sessions')
                assert 'idle' in rows and rows.count('parent=11111111') == 2, rows
                event('claude', 'done', PARENT, workers[0].pid)
                assert 'real=done display=done' in request('signs fan')
                assert request('seen') == 'ok'
                assert 'real=done display=working' in request('signs fan')
                # Remaining active count updates on completion and on real pidfd exit.
                event('codex', 'done', PARENT + 1, workers[1].pid)
                assert 'badges=1 children=1' in request('signs fan')
                workers[2].terminate()
                workers[2].wait(timeout=3)
                wait_until(lambda: 'badges=0 children=0 real=done display=done'
                           in request('signs fan'), 2,
                           description='pidfd removes child and restores parent')
                event('claude', 'idle', PARENT, workers[0].pid)
                assert 'hits=0 badges=0' in request('signs fan')
                assert request('state') == 'idle'
                client.sendall(b'stop')
                assert app.wait(timeout=3) == 0
                app = None
            print('Subagent sign state, unread acknowledgement, badge and pidfd passed.')
        finally:
            if app is not None:
                app.terminate()
                app.wait(timeout=3)
            for worker in workers:
                if worker.poll() is None:
                    worker.terminate()
                worker.wait(timeout=3)


if __name__ == '__main__':
    run()
