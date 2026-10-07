#!/usr/bin/env python3
"""Socket/pidfd integration with an injected fake /proc tree.

Requires build/agent_children_fixture. This fixture runs the production
session request parser/model/labels and pidfd loop, without a compositor.
"""
import socket
import subprocess
import tempfile
from pathlib import Path

from runtime_test_helpers import wait_until

FIXTURE = str(Path('build/agent_children_fixture').resolve())


def run():
    with tempfile.TemporaryDirectory(prefix='herdcat-children-runtime-') as directory:
        root = Path(directory)
        workers = [subprocess.Popen(['sleep', '60']) for _ in range(6)]
        app = None
        try:
            def process(index, parent):
                pid = workers[index].pid
                path = root / str(pid)
                path.mkdir(exist_ok=True)
                (path / 'stat').write_text(f'{pid} (fake ) ( agent) S {parent} 1 1 0 0\n')
                return pid

            parent = process(0, 1)
            codex = process(1, parent)
            kimi = process(2, codex)
            second = process(3, parent)
            pi = process(4, parent)
            independent = process(5, 1)
            client, server = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            with client, server:
                app = subprocess.Popen([FIXTURE, str(root), str(server.fileno())],
                                       pass_fds=(server.fileno(),))
                server.close()
                client.settimeout(3)

                def request(message):
                    client.sendall(message.encode())
                    return client.recv(4096).decode()

                def event(agent, event, key, pid, candidate=None, metadata=False):
                    message = f'ev {agent} {event} {key:016x} {pid}'
                    if candidate is not None:
                        message += f' {candidate} {int(metadata)}'
                    assert request(message) == 'ok', message

                # Child first: independently displayed until a metadata retry.
                event('codex', 'working', 0x2222222200000000, 0, codex)
                assert 'parent=' not in request('sessions')
                event('claude', 'start', 0x1111111100000000, parent)
                event('codex', 'working', 0x2222222200000000, 0, codex)
                assert 'parent=' not in request('sessions')
                assert request('name 2222222200000000 child') == 'ok'
                rows = request('sessions')
                assert 'parent=11111111' in rows and f'pid={codex}' in rows, rows
                assert request('labels') == 'Claude + Codex | Claude +1\n'
                event('kimi', 'waiting', 0x3333333300000000, kimi)
                assert request('labels') == 'Claude + Codex + Kimi | Claude +2\n'
                assert request('state') == 'idle'  # parent's own state
                event('codex', 'working', 0x4444444400000000, second)
                assert request('labels') == 'Claude + Codex ×2 + Kimi | Claude +3\n'
                event('pi', 'working', 0x5555555500000000, pi)
                assert request('labels') == 'Claude + Codex ×2 + 2 | Claude +4\n'
                event('pi', 'fail', 0x5555555500000000, pi)
                rows = request('sessions')
                assert 'unread' not in rows and request('state') == 'idle', rows
                assert request('labels') == 'Claude + Codex ×2 + Kimi | Claude +3\n'
                event('codex', 'done', 0x4444444400000000, second)
                assert request('labels') == 'Claude + Codex + Kimi | Claude +2\n'
                # Exit without END; the real pidfd must remove the row promptly.
                workers[2].terminate()
                workers[2].wait(timeout=3)
                wait_until(lambda: '33333333' not in request('sessions'), 2,
                           description='child pidfd removal')
                assert request('labels') == 'Claude + Codex | Claude +1\n'
                event('codex', 'end', 0x2222222200000000, 0)
                assert request('labels') == 'Claude | Claude\n'
                event('codex', 'waiting', 0x6666666600000000, independent)
                assert request('state') == 'waiting'
                assert 'parent=' not in next(row for row in request('sessions').splitlines()
                                             if '66666666' in row)
                request('ev codex end 6666666600000000 0')
                wait_until(lambda: '44444444' not in request('sessions') and
                           '55555555' not in request('sessions'), 3,
                           description='child completion timeout')
                client.sendall(b'stop')
                assert app.wait(timeout=3) == 0
                app = None
            print('Agent children socket, fake ancestry, labels and pidfd integration passed.')
        finally:
            if app is not None:
                app.terminate()
                app.wait(timeout=3)
            for worker in workers:
                worker.terminate()
                worker.wait(timeout=3)


if __name__ == '__main__':
    run()
