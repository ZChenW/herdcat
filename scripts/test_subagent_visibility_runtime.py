#!/usr/bin/env python3
"""Control -> overlay visibility with active children, on two fixture outputs.

Requires Wayland/control sockets. Not runnable in the restricted sandbox.
Count badge pixels and draw order are validated by test_subagent_badge.
"""
from pathlib import Path
import socket
import subprocess
import tempfile

from runtime_test_helpers import runtime_env, wait_settled, wait_until

BINARY = str(Path('build/herdcat').resolve())
COMPOSITOR = str(Path('build/compositor/server').resolve())
PARENT = 0x1111111100000000


def run():
    with tempfile.TemporaryDirectory(prefix='herdcat-subagent-visibility-') as directory:
        root = Path(directory)
        config = root / 'cat.conf'
        base = ('keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
                'monitor=TEST-1,TEST-2\noverlay_opacity=0\n'
                'cat_height=110\noverlay_height=120\ncat_y_offset=0\n'
                'sign_idle=hover\nsign_animations=off\nsign_max=10\n'
                'hotplug_scan_interval=0\ndisable_fullscreen_hide=1\n'
                'agent_stale_timeout=0\nagent_done_timeout=30\n')
        config.write_text(base + 'sign_style=fan\n')
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1')
        workers = [subprocess.Popen(['sleep', '90']) for _ in range(3)]
        server_log, app_log = root / 'server.log', root / 'app.log'
        server = app = None
        try:
            with server_log.open('w') as server_file, app_log.open('w') as app_file:
                server = subprocess.Popen([COMPOSITOR], env=env, stdin=subprocess.PIPE,
                                          stdout=server_file, stderr=server_file, text=True)

                def diagnostics():
                    return server_log.read_text()[-4000:] + app_log.read_text()[-2000:]

                def wire(message):
                    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
                        client.settimeout(3)
                        client.connect(str(root / 'herdcat.sock'))
                        client.sendall(message.encode())
                        response = client.recv(8192).decode()
                    assert response.startswith('0 '), (message, response)
                    return response[2:]

                def regions():
                    counts = {}
                    for line in server_log.read_text().splitlines():
                        fields = line.split()
                        if fields and fields[0] == 'input-count':
                            counts[fields[1]] = int(fields[2])
                    return counts

                def visible(expected):
                    return wait_settled(regions,
                                        ready=lambda counts: all(counts.get(m) == expected
                                                                 for m in ('TEST-1', 'TEST-2')),
                                        description=f'{expected} committed input regions',
                                        diagnostics=diagnostics)

                wait_until(lambda: (root / 'wayland-test').exists(), diagnostics=diagnostics)
                app = subprocess.Popen([BINARY, '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait_until(lambda: (root / 'herdcat.sock').exists(), diagnostics=diagnostics)
                wire(f'ev claude start {PARENT:016x} {workers[0].pid}')
                visible(1)  # only the cat; idle parent is retracted
                for i, state in enumerate(('working', 'waiting'), 1):
                    pid = workers[i].pid
                    wire(f'ev codex {state} {PARENT+i:016x} 0 {pid} 0 {workers[0].pid}')
                visible(2)  # cat + parent's plate, never children's plates
                rows = wire('sessions')
                assert rows.count('parent=11111111') == 2, rows
                assert next(line for line in rows.splitlines()
                            if line.startswith('claude ')).split()[2] == 'idle', rows
                assert 'agent=idle' in wire('status')
                for style in ('post', 'fan'):
                    config.write_text(base + f'sign_style={style}\n')
                    wire('reload')
                    visible(2)
                workers[1].terminate()
                workers[1].wait(timeout=3)
                wait_until(lambda: wire('sessions').count('parent=11111111') == 1,
                           description='first child pidfd', diagnostics=diagnostics)
                visible(2)  # remaining waiting child still keeps the parent up
                workers[2].terminate()
                workers[2].wait(timeout=3)
                wait_until(lambda: 'parent=' not in wire('sessions'),
                           description='last child pidfd', diagnostics=diagnostics)
                visible(1)
                assert 'agent=idle' in wire('status')
                app.terminate()
                assert app.wait(timeout=3) == 0
                assert 'AddressSanitizer' not in app_log.read_text()
                assert 'runtime error:' not in app_log.read_text()
                print('Subagent visibility, real status, both outputs and pidfd restore passed.')
        finally:
            for process in (app, server, *workers):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
            if server is not None:
                server.stdin.close()


if __name__ == '__main__':
    run()
