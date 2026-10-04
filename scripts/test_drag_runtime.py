#!/usr/bin/env python3
"""Exercise pointer events, regions and persisted positions on two outputs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

binary = str(Path('build/bongocat').resolve())
fixture = str(Path('build/compositor/server').resolve())
parser = argparse.ArgumentParser()
parser.add_argument('--sign-style', choices=('fan', 'post', 'off'), default='fan')
style = parser.parse_args().sign_style
# A 40px cat in a 50px bar has enough desk space already.
clearance = {'fan': 38, 'post': 60, 'off': 0}[style]
max_margin = 600 - (50 + clearance)


def wait_for(condition):
    end = time.monotonic() + 4
    while time.monotonic() < end:
        if condition():
            return
        time.sleep(.03)
    raise AssertionError('drag fixture condition timed out')


with tempfile.TemporaryDirectory(prefix='bongocat-drag-runtime-') as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
               WAYLAND_DISPLAY='wayland-test', BONGOCAT_TEST_DRAG='1')
    config = root / 'cat.conf'
    base = ('monitor=TEST-1,TEST-2\noverlay_opacity=0\noverlay_position=bottom\n'
            'disable_fullscreen_hide=1\ncat_x_offset=0\ncat_y_offset=0\n'
            '[monitor:TEST-2]\noverlay_position=top\n[global]\n')
    base += f'sign_style={style}\n'
    config.write_text(base)
    position = root / 'bongocat/position'
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

    def regions():
        result = {}
        for line in server_log.read_text().splitlines():
            parts = line.split()
            if parts and parts[0] == 'input':
                result[parts[1]] = tuple(map(int, parts[2:]))
        return result

    try:
        wait_for(lambda: (root / 'wayland-test').exists())
        app = start()
        wait_for(lambda: len(regions()) == 2 and all(v[2] for v in regions().values()))
        start_x = regions()['TEST-1'][0]
        second_x = regions()['TEST-2'][0]
        send('drag TEST-1 0 0')
        time.sleep(.15)
        assert not position.exists(), 'click must not save a position'
        send('drag TEST-1 20 -40')
        wait_for(lambda: records().get('TEST-1') == (start_x + 20, 40))
        assert 'margin TEST-1 0 40' in server_log.read_text()
        send('drag TEST-2 -30 45')
        wait_for(lambda: records().get('TEST-2') == (second_x - 30, 45))
        assert records()['TEST-1'] == (start_x + 20, 40)
        assert 'margin TEST-2 45 0' in server_log.read_text()
        saved = records()
        app.terminate()
        assert app.wait(timeout=3) == 0
        app = start()
        wait_for(lambda: (root / 'bongocat.sock').exists())
        wait_for(lambda: regions()['TEST-1'][0] == saved['TEST-1'][0])
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
        send('leave TEST-1 10 -10')
        wait_for(lambda: records()['TEST-1'] == (start_x + 30, 50))
        send('lost TEST-1 10 -10')
        wait_for(lambda: records()['TEST-1'] == (start_x + 40, 60))
        send('capabilities')
        time.sleep(.1)
        send('drag TEST-1 10000 -10000')
        wait_for(lambda: records()['TEST-1'] == (728, max_margin))
        send('out TEST-1')
        time.sleep(.7)  # Close the hover pad before reading the cat rectangle.
        send('step')  # Fractional scale and smaller TEST-1.
        wait_for(lambda: regions()['TEST-1'][0] == 568)
        send('step')  # Remove TEST-2.
        time.sleep(.2)
        send('step')  # Re-add TEST-2 and restore its own saved position.
        wait_for(lambda: server_log.read_text().count('overlay TEST-2') >= 3)
        assert records()['TEST-2'] == saved['TEST-2']
        command('reset-position')
        wait_for(lambda: not position.exists())
        wait_for(lambda: regions()['TEST-1'][0] == 284)
        assert 'margin TEST-1 0 0' in server_log.read_text()
        assert 'margin TEST-2 0 0' in server_log.read_text()
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
