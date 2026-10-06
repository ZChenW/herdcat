#!/usr/bin/env python3
"""Exercise pointer events, regions and persisted positions on two outputs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_settled, wait_until

binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())
parser = argparse.ArgumentParser()
parser.add_argument('--sign-style', choices=('fan', 'post', 'off'), default='fan')
style = parser.parse_args().sign_style
# A 40px cat in a 50px bar has enough desk space already.
# Fan and post follow sign_clearance(): (cat_height * design + 109) / 110.
# Both designs are 180 since the switch card has four rows, which is 66px at
# the default 40px cat.
clearance = {'fan': 66, 'post': 66, 'off': 0}[style]
max_margin = 600 - (50 if style == 'off' else 42)


def wait_for(condition, seconds=6):
    return wait_until(condition, seconds, description='test_drag_runtime.py condition',
                      diagnostics=lambda: server_log.read_text()[-4000:] + app_log.read_text()[-2000:])


with tempfile.TemporaryDirectory(prefix='herdcat-drag-runtime-') as directory:
    root = Path(directory)
    env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
               WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1')
    config = root / 'cat.conf'
    base = ('keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'hotplug_scan_interval=0\nmonitor=TEST-1,TEST-2\noverlay_opacity=0\noverlay_position=bottom\n'
            'disable_fullscreen_hide=1\ncat_x_offset=0\ncat_y_offset=0\n'
            '[monitor:TEST-2]\noverlay_position=top\n[global]\n')
    base += f'sign_style={style}\n'
    config.write_text(base)
    position = root / 'herdcat/position'
    server_log = root / 'server.log'
    app_log = root / 'app.log'
    server_file = server_log.open('w')
    app_file = app_log.open('w')
    server = subprocess.Popen([fixture], env=env, stdin=subprocess.PIPE,
                              stdout=server_file, stderr=server_file, text=True)
    app = None

    def start():
        return subprocess.Popen([binary, '-c', str(config), '-w'], env=env,
                                stdout=app_file, stderr=app_file)

    def command(name):
        subprocess.run([binary, '--' + name], env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       timeout=3)

    def send(line):
        server.stdin.write(line + '\n')
        server.stdin.flush()

    def records():
        if not position.exists():
            return {}
        return {name: (int(x), int(y))
                for name, x, y in (line.split() for line in position.read_text().splitlines())}

    def placements():
        result = {}
        for line in server_log.read_text().splitlines():
            parts = line.split()
            if parts[:1] == ['placement'] and parts[2] == 'herdcat-overlay':
                result[parts[1]] = tuple(map(int, parts[3:]))
        return result

    def local_regions():
        result = {}
        for line in server_log.read_text().splitlines():
            parts = line.split()
            if parts and parts[0] == 'input':
                result[parts[1]] = tuple(map(int, parts[2:]))
        return result

    def regions():
        # Position files retain output coordinates. Input is now surface-local.
        placed = placements()
        return {name: (rect[0] + placed[name][3], *rect[1:])
                for name, rect in local_regions().items() if name in placed}

    def settled():
        return wait_settled(lambda: (placements(), local_regions()),
                            ready=lambda data: bool(data[0]) and bool(data[1]),
                            description='drag placements and input regions',
                            diagnostics=lambda: server_log.read_text()[-5000:])

    def position_is(name, expected):
        actual = records().get(name)
        return actual is not None and all(abs(a - b) <= 1
                                          for a, b in zip(actual, expected))

    try:
        wait_for(lambda: (root / 'wayland-test').exists())
        app = start()
        wait_for(lambda: len(regions()) == 2 and all(v[2] for v in regions().values()))
        wait_for(lambda: placements().get('TEST-1', (0,) * 6)[4] ==
                 (72 if style == 'off' else 240))
        settled()
        start_x = regions()['TEST-1'][0]
        second_x = regions()['TEST-2'][0]
        send('drag TEST-1 0 0')
        settled()
        assert not position.exists(), 'click must not save a position'
        send('drag TEST-1 20 -40')
        wait_for(lambda: position_is('TEST-1', (start_x + 20, 40)))
        # The margin now travels with the next buffer commit, not ahead of it.
        wait_for(lambda: 'margin TEST-1 0 40' in server_log.read_text())
        settled()
        assert placements()['TEST-1'][3] > 0
        send('drag TEST-2 -30 45')
        wait_for(lambda: position_is('TEST-2', (second_x - 30, 45)))
        assert position_is('TEST-1', (start_x + 20, 40))
        wait_for(lambda: f"margin TEST-2 {45 if style == 'off' else 42} 0" in server_log.read_text())
        saved = records()
        app.terminate()
        assert app.wait(timeout=3) == 0
        app = start()
        wait_for(lambda: (root / 'herdcat.sock').exists())
        wait_for(lambda: abs(regions()['TEST-1'][0] - saved['TEST-1'][0]) <= 1)
        assert records() == saved
        for op in ('pause', 'resume', 'hide'):
            command(op)
        wait_for(lambda: all(v[2] == 0 for v in regions().values()))
        command('show')
        wait_for(lambda: all(v[2] for v in regions().values()))
        config.write_text(base + 'cat_draggable=0\n')
        command('reload')
        wait_for(lambda: all(v[2] == 0 for v in regions().values()))
        config.write_text(base)
        command('reload')
        wait_for(lambda: all(v[2] for v in regions().values()))
        assert records() == saved
        # Resize and mirror on reload without changing output-space records.
        config.write_text(base + 'cat_height=60\nmirror_x=1\ncat_align=right\n')
        command('reload')
        wait_for(lambda: placements()['TEST-1'][4] == (108 if style == 'off' else 356))
        assert records() == saved
        config.write_text(base)
        command('reload')
        wait_for(lambda: placements()['TEST-1'][4] == (72 if style == 'off' else 240))
        send('leave TEST-1 10 -10')
        wait_for(lambda: position_is('TEST-1', (start_x + 30, 50)))
        send('lost TEST-1 10 -10')
        wait_for(lambda: position_is('TEST-1', (start_x + 40, 60)))
        send('capabilities')
        settled()
        send('drag TEST-1 10000 -10000')
        wait_for(lambda: position_is('TEST-1', (728, max_margin)))
        send('out TEST-1')
        settled()  # Wait for the hover pad to close before sampling.
        send('step')  # Fractional scale and smaller TEST-1.
        wait_for(lambda: abs(regions()['TEST-1'][0] - 568) <= 1)
        settled()
        assert abs(local_regions()['TEST-1'][0] - (0 if style == 'off' else 166)) <= 1, (placements(), local_regions())
        send('step')  # Remove TEST-2.
        settled()
        send('step')  # Re-add TEST-2 and restore its own saved position.
        wait_for(lambda: server_log.read_text().count('overlay TEST-2') >= 3)
        assert records()['TEST-2'] == saved['TEST-2']
        command('reset-position')
        wait_for(lambda: not position.exists())
        wait_for(lambda: abs(regions()['TEST-1'][0] - 284) <= 1)
        settled()
        assert abs(local_regions()['TEST-1'][0] - (0 if style == 'off' else 84)) <= 1, (placements(), local_regions())
        wait_for(lambda: 'margin TEST-1 0 0' in server_log.read_text())
        assert abs(placements()['TEST-1'][3] - (284 if style == 'off' else 200)) <= 1, placements()
        wait_for(lambda: 'margin TEST-2 0 0' in server_log.read_text())
        app.terminate()
        assert app.wait(timeout=3) == 0
        assert 'ERROR: AddressSanitizer' not in app_log.read_text()
        assert 'runtime error:' not in app_log.read_text()
        print(style + ': drag regions, both anchors, per-output persistence, reset, '
              'clamping, scale, reconnect and pointer loss passed.')
    except Exception:
        print(server_log.read_text()[-7000:])
        print(app_log.read_text()[-4000:])
        raise
    finally:
        for process in (app, server):
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
        server.stdin.close()
        server_file.close()
        app_file.close()
