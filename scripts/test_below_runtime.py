#!/usr/bin/env python3
"""Check committed cat/sign placement across orientation changes in isolation."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_settled, wait_until

BINARY = str(Path('build/herdcat').resolve())
FIXTURE = str(Path('build/compositor/server').resolve())


def run(style, theme, top, sign_max):
    design = 180 if sign_max <= 5 else 273 if style == 'fan' else 345
    clearance = (40 * design + 109) // 110
    with tempfile.TemporaryDirectory(prefix='herdcat-below-runtime-') as directory:
        root = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory,
                   XDG_STATE_HOME=directory, WAYLAND_DISPLAY='wayland-test',
                   HERDCAT_TEST_DRAG='1')
        env.pop('NIRI_SOCKET', None)
        config = root / 'cat.conf'
        config.write_text(
            'keyboard_device=/dev/input/herdcat-runtime-nonexistent\nmonitor=TEST-1\noverlay_opacity=0\ncat_height=40\n'
            'overlay_height=50\ncat_y_offset=0\ncat_x_offset=0\n'
            'disable_fullscreen_hide=1\nhotplug_scan_interval=0\n'
            f'overlay_position={"top" if top else "bottom"}\n'
            f'sign_style={style}\nsign_theme={theme}\nsign_animations=off\n'
            f'sign_max={sign_max}\n')
        log = root / 'server.log'
        with log.open('w') as server_file, (root / 'app.log').open('w') as app_file:
            server = subprocess.Popen([FIXTURE], env=env, stdin=subprocess.PIPE,
                                      stdout=server_file, stderr=server_file,
                                      text=True)
            app = None

            def wait_for(condition, seconds=6):
                return wait_until(condition, seconds,
                                  description='below runtime condition',
                                  diagnostics=lambda: log.read_text()[-4000:] +
                                  (root / 'app.log').read_text()[-2000:])

            def snapshots():
                return [tuple(map(int, line.split()[2:]))
                        for line in log.read_text().splitlines()
                        if line.startswith('snapshot TEST-1 ')]

            def snapshot():
                records = snapshots()
                return records[-1] if records else None

            def settled():
                return wait_settled(snapshot, description='below placement',
                                    diagnostics=lambda: log.read_text()[-4000:] +
                                    (root / 'app.log').read_text()[-2000:])

            def send(line):
                server.stdin.write(line + '\n')
                server.stdin.flush()

            def cat_output(record):
                # top, bottom, left, width, height, then the first (cat) rect.
                origin = record[0] if top else 600 - record[1] - record[4]
                return origin + record[6]

            def check_bounds(record):
                origin = record[0] if top else 600 - record[1] - record[4]
                assert 0 <= origin <= 600 - record[4], record
                assert record[7:9] == (72, 40), record
                assert record[4] == 50 + clearance, record
                design_extent = 820 if style == 'post' else 652
                width = (40 * design_extent + 109) // 110
                # TEST-1 initially uses 150/120 scaling: align its surface
                # to the four-logical-pixel origin grid on an 800px output.
                assert record[3] == width + (800 - width) % 4, record

            def drag(dy, expected):
                # Enter a cat directly so the sign pad cannot become the drag
                # target; the fixture's drag uses its last input rectangle.
                send('out TEST-1')
                settled()
                before = len(snapshots())
                send(f'drag TEST-1 0 {dy}')
                # A drag that cannot move the cat (already at the edge) commits
                # nothing new, so do not insist on a fresh snapshot.
                deadline = time.monotonic() + 1
                while len(snapshots()) == before and time.monotonic() < deadline:
                    time.sleep(.04)
                record = settled()
                assert abs(cat_output(record) - expected) <= 1, record
                for item in snapshots()[before:]:
                    check_bounds(item)
                return record

            try:
                wait_for(lambda: (root / 'wayland-test').exists())
                app = subprocess.Popen([BINARY, '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait_for(lambda: snapshot() and snapshot()[7] > 0)
                start = settled()
                check_bounds(start)
                # Both initial anchors and a very large drag reach y=0.
                record = drag(-10000, 0)
                assert abs(record[6]) <= 1, record
                position = root / 'herdcat/position'
                wait_for(position.exists)
                saved = position.read_text()
                app.terminate()
                assert app.wait(timeout=3) == 0
                before = len(snapshots())
                app = subprocess.Popen([BINARY, '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait_for(lambda: len(snapshots()) > before and
                         snapshot()[7] > 0)
                record = settled()
                assert abs(cat_output(record)) <= 1, record
                assert position.read_text() == saved
                # Stay below until the configured clearance + 24.
                record = drag(clearance + 23, clearance + 23)
                assert record[6] <= 3, record
                record = drag(5, clearance + 28)
                assert abs(record[6] - (clearance + 8)) <= 1, record
                record = drag(-29, clearance - 1)
                assert record[6] <= 3, record
                # A waiting sign's input region must extend beneath the cat.
                subprocess.run([BINARY, '--state', 'waiting'], env=env,
                               check=True, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, timeout=3)
                send('out TEST-1')
                record = settled()
                assert record[10] + record[12] > record[6] + 40, record
                # A below switch card has the font row in the reflected half.
                x, y = record[5] + 36, record[6] + 20
                send(f'tap TEST-1 {x} {y} 273')
                wait_for(lambda: snapshot()[12] >= 59 and
                         snapshot()[10] >= snapshot()[6] + 39)
                record = settled()
                assert record[10] >= record[6] + 39, record
                app.terminate()
                assert app.wait(timeout=3) == 0
                text = (root / 'app.log').read_text()
                assert 'ERROR: AddressSanitizer' not in text
                assert 'runtime error:' not in text
                print(style, theme, 'top' if top else 'bottom', sign_max, 'passed')
            except Exception:
                print(log.read_text()[-6000:])
                print((root / 'app.log').read_text()[-3000:])
                raise
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


for style in ('fan', 'post'):
    for theme in ('light', 'dark'):
        for top in (False, True):
            for sign_max in (5, 10):
                run(style, theme, top, sign_max)
