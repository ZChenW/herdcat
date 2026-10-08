#!/usr/bin/env python3
"""Check committed cat/sign placement across orientation changes in isolation."""
import os
import socket
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
    reach = (40 * (114 if style == "fan" and sign_max <= 5 else
                   190 if style == "fan" else 171 if sign_max <= 5 else 329) + 109) // 110
    with tempfile.TemporaryDirectory(prefix='hc-b-', dir='/tmp') as directory:
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
            f'sign_max={sign_max}\nsign_idle=always\nagent_stale_timeout=0\n')
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
                assert record[4] == 50 + clearance + 3, record
                design_extent = 820 if style == 'post' else 652
                width = (40 * design_extent + 109) // 110
                # TEST-1 initially uses 150/120 scaling: align its surface
                # to the four-logical-pixel origin grid on an 800px output.
                assert record[3] == width + (800 - width) % 4, record

            def drag(dy, expected):
                # Enter a cat directly so the sign pad cannot become the drag
                # target; the fixture's drag uses the cat input rectangle.
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
                wait_for(lambda: (root / 'herdcat.sock').exists())
                wait_for(lambda: snapshot() and snapshot()[7] > 0)
                cat = snapshot()
                send(f'hover TEST-1 {cat[5] + 36} {cat[6] + 20}')
                # Keep this suite's full configured clearance checks; the new
                # surface-tier matrix checks the smaller committed surfaces.
                for key in range(1, sign_max + 1):
                    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                        control.settimeout(3)
                        control.connect(str(root / 'herdcat.sock'))
                        control.sendall(f'ev claude working {key:016x} 0'.encode())
                        assert control.recv(512).startswith(b'0 ')
                wait_for(lambda: snapshot() and snapshot()[7] > 0 and
                         snapshot()[4] == 50 + clearance + 3)
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
                # This full tier stays below until its current clearance + 24.
                record = drag(reach + 23, reach + 23)
                assert record[6] <= 3, record
                record = drag(5, reach + 28)
                assert abs(record[6] - min(clearance + 8, reach + 28)) <= 1, record
                record = drag(-29, reach - 1)
                assert record[6] <= 3, record
                # A waiting sign's input region must extend beneath the cat.
                subprocess.run([BINARY, '--state', 'waiting'], env=env,
                               check=True, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, timeout=3)
                send('out TEST-1')
                record = settled()
                assert record[10] + record[12] > record[6] + 40, record
                if style == 'post':
                    regions = [tuple(map(int, line.split()[2:]))
                               for line in log.read_text().splitlines()
                               if line.startswith('sign-input TEST-1 ')]
                    # Waiting always expands: total row width includes the
                    # state face, gap and paper pill at the 40/110 cat scale.
                    assert regions and 54 <= regions[-1][2] <= 125, regions[-1:]
                    assert 10 <= regions[-1][3] <= 11, regions[-1:]
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


def tier_flip(style, top):
    """A sixth board must enter below, after capacity growth, on every commit."""
    small_clearance = 42 if style == "fan" else 63
    large_clearance = 100 if style == 'fan' else 126
    target = 69 if style == "fan" else 94
    with tempfile.TemporaryDirectory(prefix='hc-f-', dir='/tmp') as directory:
        root = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
                          HERDCAT_TEST_SURFACE_TIERS='1')
        config = root / 'cat.conf'
        config.write_text(
            'keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'monitor=TEST-1\noverlay_opacity=0\ncat_height=40\n'
            'overlay_height=50\ncat_y_offset=0\ncat_x_offset=0\n'
            'disable_fullscreen_hide=1\nhotplug_scan_interval=0\n'
            f'overlay_position={"top" if top else "bottom"}\n'
            f'sign_style={style}\nsign_theme=light\nsign_animations=full\n'
            'sign_max=10\nsign_idle=always\nagent_stale_timeout=0\n')
        log, app_log = root / 'server.log', root / 'app.log'
        with log.open('w') as server_file, app_log.open('w') as app_file:
            server = subprocess.Popen([FIXTURE], env=env, stdin=subprocess.PIPE,
                                      stdout=server_file, stderr=server_file, text=True)
            app = None

            def diagnostics():
                return log.read_text()[-6000:] + app_log.read_text()[-2000:]

            def records():
                return [tuple(map(int, line.split()[3:]))
                        for line in log.read_text().splitlines()
                        if line.startswith('tier-submit TEST-1 ')]

            def latest():
                rows = records()
                return rows[-1] if rows else None

            def wait(condition, seconds=8):
                return wait_until(condition, seconds, diagnostics=diagnostics)

            def settle():
                return wait_settled(latest, diagnostics=diagnostics)

            def send(line):
                server.stdin.write(line + '\n')
                server.stdin.flush()

            def wire(state, key):
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                    control.settimeout(3)
                    control.connect(str(root / 'herdcat.sock'))
                    control.sendall(f'ev claude {state} {key:016x} 0'.encode())
                    assert control.recv(512).startswith(b'0 ')

            def cat_output(item):
                origin = item[6] if top else 600 - item[7] - item[10]
                return origin + item[12]

            try:
                wait(lambda: (root / 'wayland-test').exists())
                app = subprocess.Popen([BINARY, '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait(lambda: (root / 'herdcat.sock').exists())
                wait(lambda: latest() and latest()[13] >= 1)
                cat = latest()
                send(f'hover TEST-1 {cat[11] + 36} {cat[12] + 20}')
                for key in range(1, 6):
                    wire('working', key)
                wait(lambda: latest() and latest()[10] == 119 and latest()[13] >= 6)
                start = settle()
                send('out TEST-1')
                send(f'drag TEST-1 0 {target - cat_output(start)}')
                wait(lambda: abs(cat_output(latest()) - target) <= 1)
                small = settle()
                send('out TEST-1')
                settle()  # remove the hover pad before counting six boards
                assert small[12] >= small_clearance, small  # still above
                position = root / 'herdcat/position'
                wait(position.exists)
                saved = position.read_bytes()
                before = len(records())
                wire('working', 6)
                wait(lambda: latest()[10] == 50 + large_clearance + 3 and
                     latest()[13] >= 7)
                large = settle()
                assert large[12] <= 6, large  # flipped before sixth entry
                assert abs(cat_output(large) - target) <= 1, large
                for key in range(7, 11):
                    wire('working', key)
                wait(lambda: latest()[13] >= 11)
                settle()
                for key in range(6, 11):
                    wire('end', key)
                rest_height = 84 if style == 'fan' else 119
                wait(lambda: latest()[10] == rest_height and latest()[9] == 80, 15)
                reduced = settle()
                expected_y = 31 if style == 'fan' else 74
                assert expected_y <= reduced[12] <= expected_y + 3, reduced
                assert abs(cat_output(reduced) - target) <= 1, reduced
                assert position.read_bytes() == saved
                for item in records()[before:]:
                    # Check actual submitted alpha as well as input geometry.
                    w, h, x0, y0, x1, y1, mt, mb, ml, sw, sh, cx, cy, count = item
                    origin = mt if top else 600 - mb - sh
                    assert 0 <= origin <= 600 - sh, item
                    assert 0 <= y0 <= y1 < h, item
                    assert origin + y0 * 120 / 150 >= 0, item
                    if origin > 0:
                        assert y0 > 0, item  # no ink clipped at the upper edge
                    assert abs(cat_output(item) - target) <= 1, item
                    if count >= 7:
                        assert sh == 50 + large_clearance + 3 and cy <= 6, item
                assert 'AddressSanitizer' not in app_log.read_text()
                assert 'runtime error:' not in app_log.read_text()
                print('Tier flip', style, 'top' if top else 'bottom',
                      '5 -> 6 -> 10 -> 5; every submitted frame bounded; position unchanged')
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


def split_card(top):
    """Fan at y=120 stays above; card and its font anchor open below."""
    with tempfile.TemporaryDirectory(prefix='hc-card-', dir='/tmp') as directory:
        root = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
                          HERDCAT_TEST_SURFACE_TIERS='1', HERDCAT_TEST_TIER_SCALE='120')
        config = root / 'cat.conf'
        config.write_text(
            'keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'monitor=TEST-1\noverlay_opacity=0\ncat_height=110\n'
            'overlay_height=120\nsign_style=fan\nsign_max=5\n'
            'sign_idle=always\nsign_animations=full\nagent_stale_timeout=0\n'
            'disable_fullscreen_hide=1\nhotplug_scan_interval=3600\n'
            f'overlay_position={"top" if top else "bottom"}\n')
        log, app_log = root / 'server.log', root / 'app.log'
        with log.open('w') as server_file, app_log.open('w') as app_file:
            server = subprocess.Popen([FIXTURE], env=env, stdin=subprocess.PIPE,
                                      stdout=server_file, stderr=server_file, text=True)
            app = None

            def diagnostics():
                return log.read_text()[-5000:] + app_log.read_text()[-2000:]

            def records():
                return [tuple(map(int, line.split()[3:]))
                        for line in log.read_text().splitlines()
                        if line.startswith('tier-submit TEST-1 ')]

            def latest():
                rows = records()
                return rows[-1] if rows else None

            def cat_y(row):
                return (row[6] if top else 600 - row[7] - row[10]) + row[12]

            def send(line):
                server.stdin.write(line + '\n')
                server.stdin.flush()

            def snapshots():
                return [tuple(map(int, line.split()[2:]))
                        for line in log.read_text().splitlines()
                        if line.startswith('snapshot TEST-1 ')]

            def wait(condition):
                return wait_until(condition, 8, diagnostics=diagnostics)

            try:
                wait(lambda: (root / 'wayland-test').exists())
                app = subprocess.Popen([BINARY, '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait(lambda: (root / 'herdcat.sock').exists())
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                    control.connect(str(root / 'herdcat.sock'))
                    control.sendall(b'ev claude working 0000000000000001 0')
                    assert control.recv(512).startswith(b'0 ')
                wait(lambda: latest() and latest()[13] >= 2)
                start = wait_settled(latest, diagnostics=diagnostics)
                send(f'drag TEST-1 0 {200 - cat_y(start)}')
                wait(lambda: cat_y(latest()) == 200)
                wait_settled(latest, diagnostics=diagnostics)
                send('drag TEST-1 0 -80')
                wait(lambda: cat_y(latest()) == 120)
                wait_settled(latest, diagnostics=diagnostics)
                send('out TEST-1')
                start = wait_settled(latest, diagnostics=diagnostics)
                assert start[12] > 8, start  # signs are above, not below
                before = len(records())
                send(f'tap TEST-1 {start[11] + 99} {start[12] + 55} 273')
                wait(lambda: latest()[10] == 488 and snapshots()[-1][12] >= 168)
                card_row = wait_settled(lambda: snapshots()[-1], diagnostics=diagnostics)
                row = latest()
                assert cat_y(row) == 120 and row[12] > 8, row
                card = card_row[9:13]
                assert card[1] >= row[12] + 110, (card, row)
                assert card[1] + card[3] < row[10], (card, row)
                # Reflected font-row hit opens a panel anchored to this card.
                send(f'tap TEST-1 {card[0] + card[2] * 77 // 154} '
                     f'{card[1] + card[3] * 65 // 168} 272')
                wait(lambda: 'overlay TEST-1 herdcat-font-panel' in log.read_text())
                for item in records()[before:]:
                    origin = item[6] if top else 600 - item[7] - item[10]
                    assert cat_y(item) == 120, item
                    assert 0 <= item[3] <= item[5] < item[1], item
                    assert origin + item[3] >= 0 and origin + item[5] < 600, item
                send(f'tap TEST-1 {row[11] + 99} {row[12] + 55} 273')
                wait(lambda: 'gone herdcat-font-panel' in log.read_text())
                print('Independent below card at cat y=120, unchanged cat/sign direction; font hit passed', top)
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


if __name__ == '__main__':
    for top in (False, True):
        split_card(top)
    for style in ('fan', 'post'):
        for top in (False, True):
            tier_flip(style, top)
    for style in ('fan', 'post'):
        for theme in ('light', 'dark'):
            for top in (False, True):
                for sign_max in (5, 10):
                    run(style, theme, top, sign_max)
