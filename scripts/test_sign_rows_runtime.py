#!/usr/bin/env python3
"""Sign rows: real reload, input-region and height checks on the fixture.

Requires Unix/Wayland sockets; intentionally not runnable in the sandbox.
Model/pixel/group ordering is checked by test_sign_rows without sockets.
"""
from pathlib import Path
import socket
import subprocess
import tempfile

from runtime_test_helpers import runtime_env, wait_settled, wait_until

binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())
with tempfile.TemporaryDirectory(prefix='herdcat-sign-rows-runtime-') as directory:
    root = Path(directory)
    env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                      WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1')
    env.pop('NIRI_SOCKET', None)
    base = ('keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'monitor=TEST-1,TEST-2\noverlay_opacity=0\n'
            'cat_height=110\noverlay_height=120\ncat_y_offset=0\n'
            'sign_idle=always\nsign_animations=off\n'
            'hotplug_scan_interval=0\ndisable_fullscreen_hide=1\n'
            'agent_stale_timeout=0\n')
    config = root / 'cat.conf'
    config.write_text(base)
    server_log, app_log = root / 'server.log', root / 'app.log'

    def diagnostics():
        return server_log.read_text()[-5000:] + app_log.read_text()[-3000:]

    def wire(request, success=True):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
            control.settimeout(3)
            control.connect(str(root / 'herdcat.sock'))
            control.sendall(request.encode())
            response = control.recv(512).decode()
        assert response.startswith('0 ') == success, (request, response)
        return response

    def records():
        result = {}
        for line in server_log.read_text().splitlines():
            fields = line.split()
            if fields and fields[0] in ('snapshot', 'input-count', 'sign-input'):
                result[(fields[0], fields[1])] = tuple(map(int, fields[2:]))
        return result

    def settled():
        return wait_settled(records, ready=lambda x: ('snapshot', 'TEST-1') in x,
                            description='sign rows committed regions',
                            diagnostics=diagnostics)

    with server_log.open('w') as server_file, app_log.open('w') as app_file:
        server = subprocess.Popen([fixture], env=env, stdin=subprocess.PIPE,
                                  stdout=server_file, stderr=server_file,
                                  text=True)
        app = None
        try:
            wait_until(lambda: (root / 'wayland-test').exists(), 6,
                       diagnostics=diagnostics)
            app = subprocess.Popen([binary, '-c', str(config)], env=env,
                                   stdout=app_file, stderr=app_file)
            wait_until(lambda: (root / 'herdcat.sock').exists(), 6,
                       diagnostics=diagnostics)
            # Default capacity is ten. Creation/display order remains stable.
            for i in range(1, 11):
                wire(f'ev claude working {i:016x} 0')
            initial = settled()
            for monitor in ('TEST-1', 'TEST-2'):
                assert initial[('snapshot', monitor)][4] == 401, initial
                assert 11 <= initial[('input-count', monitor)][0] <= 12, initial
            for style in ('fan', 'post'):
                for maximum in (5, 6, 7, 10):
                    config.write_text(base + f'sign_style={style}\n'
                                      f'sign_max={maximum}\n')
                    wire('reload')
                    state = settled()
                    clearance = (180 if maximum <= 5 else 273 if style == 'fan'
                                 else 180 + 33 * (maximum - 5))
                    for monitor in ('TEST-1', 'TEST-2'):
                        record = state[('snapshot', monitor)]
                        assert record[4] == 120 + clearance + 8, state
                        # Cat + visible plates, and optionally the hover pad.
                        count = state[('input-count', monitor)][0]
                        assert maximum + 1 <= count <= maximum + 2, state
                    if style == 'post':
                        # All working Claude rows are collapsed initially.
                        for monitor in ('TEST-1', 'TEST-2'):
                            assert state[('sign-input', monitor)][2:] == (34, 28), state
                        # Hovering the cat expands both parts as one input row.
                        record = state[('snapshot', 'TEST-1')]
                        server.stdin.write(f'hover TEST-1 {record[5] + 99} {record[6] + 55}\n')
                        server.stdin.flush()
                        wait_until(lambda: records()[('sign-input', 'TEST-1')][2] >= 150, 6,
                                   diagnostics=diagnostics)
                        expanded = settled()[('sign-input', 'TEST-1')]
                        assert 150 <= expanded[2] <= 346 and expanded[3] == 28, expanded
                        server.stdin.write('out TEST-1\n')
                        server.stdin.flush()
                        wait_until(lambda: records()[('sign-input', 'TEST-1')][2] == 34, 6,
                                   diagnostics=diagnostics)
                    # State updates causing a row swap keep all ten sessions.
                    wire('ev claude waiting 0000000000000001 0')
                    state = settled()
                    assert 'sessions=10' in wire('status')
                    wire('ev claude working 0000000000000001 0')
                    # The card retracts both rows; leaving closes the card.
                    record = settled()[('snapshot', 'TEST-1')]
                    x, y = record[5] + 99, record[6] + 55
                    server.stdin.write(f'tap TEST-1 {x} {y} 273\n')
                    server.stdin.flush()
                    opened = settled()
                    assert opened[('input-count', 'TEST-1')][0] == 2, opened
                    server.stdin.write('out TEST-1\n')
                    server.stdin.flush()
                    wait_until(lambda: records().get(('input-count', 'TEST-1'),
                               (0,))[0] >= maximum + 1, 8,
                               diagnostics=diagnostics)
                # Invalid limits reject reload without changing the geometry.
                previous = settled()
                for invalid in (0, 11):
                    config.write_text(base + f'sign_max={invalid}\n')
                    wire('reload', False)
                    assert settled() == previous
            app.terminate()
            assert app.wait(timeout=3) == 0
            log = app_log.read_text()
            assert 'AddressSanitizer' not in log and 'runtime error:' not in log
            print('Sign rows capacity, height, card and reload matrix passed.')
        finally:
            for process in (app, server):
                if process and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
            server.stdin.close()
