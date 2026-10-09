#!/usr/bin/env python3
"""Surface capacity/resting acceptance on the isolated compositor.

Expected: twelve 'Surface tiers ... passed: 0 -> 1 -> 6 -> 1 -> 0' lines,
then 'Surface tier runtime matrix passed.' No SKIP or desktop connection.
"""
import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from runtime_test_helpers import control_ready, runtime_env, runtime_timing, wait_until

ROOT = Path(__file__).resolve().parent.parent


def run(style, top, scale, clamped=False, real_time=False):
    factor, timing = runtime_timing(real_time, divisor=10)
    with tempfile.TemporaryDirectory(prefix='ht-', dir='/tmp') as directory:
        root = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
                          HERDCAT_TEST_SURFACE_TIERS='1',
                          HERDCAT_TEST_TIER_SCALE=str(scale), **timing)
        env.pop('NIRI_SOCKET', None)
        if clamped:
            env['HERDCAT_TEST_CLAMP_HEIGHT'] = '1'
        config = root / 'cat.conf'
        config.write_text(
            'keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'monitor=TEST-1,TEST-2\noverlay_opacity=0\ncat_height=110\n'
            'overlay_height=120\nsign_idle=always\nsign_animations=full\n'
            'agent_stale_timeout=0\ndisable_fullscreen_hide=1\n'
            'idle_sleep_timeout=0\nenable_scheduled_sleep=0\n'
            'hotplug_scan_interval=3600\n'
            f'sign_style={style}\noverlay_position={"top" if top else "bottom"}\n')
        server_log, app_log = root / 'server.log', root / 'app.log'
        with server_log.open('w') as server_file, app_log.open('w') as app_file:
            server = subprocess.Popen([str(ROOT / 'build/compositor/server')],
                                      env=env, stdin=subprocess.PIPE, text=True,
                                      stdout=server_file, stderr=server_file)
            app = None

            def diagnostics():
                return server_log.read_text()[-6000:] + app_log.read_text()[-3000:]

            def records():
                result = []
                for line in server_log.read_text().splitlines():
                    fields = line.split()
                    if fields[:1] == ['tier-submit']:
                        result.append((fields[1], float(fields[2]),
                                       tuple(map(int, fields[3:]))))
                return result

            def latest():
                return {name: values for name, _, values in records()}

            def send(message):
                server.stdin.write(message + '\n')
                server.stdin.flush()

            def wire(message):
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                    control.settimeout(3)
                    control.connect(str(root / 'herdcat.sock'))
                    control.sendall(message.encode())
                    reply = control.recv(4096).decode()
                    assert reply.startswith('0 '), (message, reply)

            def wait_size(height, seconds=8, width=None):
                wait_until(lambda: len(latest()) == 2 and
                           all(item[10] == height and
                               (width is None or item[9] == min(width, 800 if name == "TEST-1" else 1024))
                               for name, item in latest().items()),
                           seconds, description=f'surface height {height}',
                           diagnostics=diagnostics)

            try:
                wait_until(lambda: (root / 'wayland-test').exists(), 6,
                           diagnostics=diagnostics)
                app = subprocess.Popen([str(ROOT / 'build/herdcat'), '-c', str(config)],
                                       env=env, stdout=app_file, stderr=app_file)
                wait_until(lambda: control_ready(root / 'herdcat.sock'), 6,
                           diagnostics=diagnostics)
                wait_size(136)
                start = len(records())
                # Place below with a top anchor and above with a bottom anchor.
                original = latest()
                for i in range(1, 2):
                    wire(f'ev claude working {i:016x} 0')
                rest = 212 if style == 'fan' else 308
                wait_until(lambda: len(latest()) == 2 and
                           all(item[13] >= 2 for item in latest().values()),
                           8, diagnostics=diagnostics)
                if clamped:
                    for name, item in latest().items():
                        send(f'hover {name} {item[11] + 99} {item[12] + 55}')
                        # One seat enters outputs in sequence. Let this lane
                        # request expansion before moving to the other lane.
                        wait_until(lambda: latest()[name][9] == 652 and
                                   latest()[name][10] ==
                                   (200 if name == 'TEST-1' else 308),
                                   8, diagnostics=diagnostics)
                    def clamped_ready():
                        items = latest()
                        return (len(items) == 2 and items['TEST-1'][10] == 200
                                and items['TEST-2'][10] == 308
                                and items['TEST-1'][13] >= 1
                                and items['TEST-2'][13] >= 2)
                    wait_until(clamped_ready, 8, description='clamped output draws',
                               diagnostics=diagnostics)
                    # Reload also makes a pending request. A constrained
                    # output must draw again, without gating the other output.
                    prior = len(records())
                    wire('reload')
                    wait_until(lambda: clamped_ready() and
                               {name for name, _, _ in records()[prior:]} ==
                               {'TEST-1', 'TEST-2'}, 8,
                               description='clamped reload draws both outputs',
                               diagnostics=diagnostics)
                    assert 'clamp-height TEST-1 308 200' in server_log.read_text()
                    assert app.poll() is None
                    print('Clamped TEST-1 accepts height 200 and keeps drawing after reload.')
                    return
                wait_size(rest, width=214 if scale == 120 else
                          214 + (2560 - 214) % (4 if scale == 150 else 1))
                rest_before = latest()
                for name, item in rest_before.items():
                    send(f'hover {name} {item[11] + 99} {item[12] + 55}')
                    wait_until(lambda: latest()[name][9] == (652 if style == 'fan' else 800 if name == 'TEST-1' else 820),
                               8, diagnostics=diagnostics)
                wait_size(308, width=652 if style == 'fan' else 820)
                for name in rest_before:
                    send(f'out {name}')
                wait_until(lambda: all(item[13] >= 2 for item in latest().values()),
                           6, diagnostics=diagnostics)
                if scale == 150:
                    # Isolate rest -> hover -> rest at unchanged board count.
                    closed_at = time.monotonic()
                    wait_size(rest, 14 * factor + (.65 if factor < 1 else 0), width=216)
                    shrink = [stamp for _, stamp, item in records()
                              if stamp > closed_at and item[9] == 216]
                    assert shrink and min(shrink) >= closed_at + 10 * factor, shrink
                    for name, item in latest().items():
                        send(f'hover {name} {item[11] + 99} {item[12] + 55}')
                        wait_until(lambda: latest()[name][9] ==
                                   (652 if style == 'fan' else
                                    800 if name == 'TEST-1' else 820),
                                   8, diagnostics=diagnostics)
                    for name in rest_before:
                        send(f'out {name}')
                for i in range(2, 7):
                    wire(f'ev claude working {i:016x} 0')
                large = 401 if style == 'fan' else 473
                wait_size(large)
                wait_until(lambda: all(item[13] >= 7 for item in latest().values()),
                           6, diagnostics=diagnostics)
                time.sleep(1.5)  # finish entry, then test exit + delay
                reduced_at = time.monotonic()
                for i in range(2, 7):
                    wire(f'ev claude end {i:016x} 0')
                time.sleep(9 * factor)
                assert all(item[10] == large for item in latest().values()), latest()
                wait_size(rest, 8)
                smaller = [stamp for _, stamp, item in records()
                           if stamp > reduced_at and item[10] == rest]
                assert smaller and min(smaller) >= reduced_at + 10 * factor, smaller
                # Cancel a pending shrink by growing again.
                wire('ev claude end 0000000000000001 0')
                time.sleep(4 * factor)
                wire('ev claude working 0000000000000001 0')
                # Transitions use the original animation clock. Observe past
                # the cancelled deadline, including its unscaled exit motion.
                time.sleep(7 * factor + (.65 if factor < 1 else 0))
                assert all(item[10] == rest for item in latest().values()), latest()
                zero_at = time.monotonic()
                wire('ev claude end 0000000000000001 0')
                wait_size(136, 14 * factor + (.65 if factor < 1 else 0))
                zeros = [stamp for _, stamp, item in records()
                         if stamp > zero_at and item[10] == 136]
                assert zeros and min(zeros) >= zero_at + 10 * factor, zeros
                # Grow beyond an in-flight small-tier request before its
                # first new pixels can be admitted; no size-only commit may
                # publish a prepared viewport or margin from that tier.
                for key in range(1, 7):
                    wire(f'ev claude working {key:016x} 0')
                wait_size(284 if style == "fan" else 473)
                wait_until(lambda: all(item[13] >= 7 for item in latest().values()),
                           6, diagnostics=diagnostics)
                if scale == 150:
                    for name, item in latest().items():
                        send(f'hover {name} {item[11] + 99} {item[12] + 55}')
                        wait_until(lambda: latest()[name][9] ==
                                   (652 if style == 'fan' else
                                    800 if name == 'TEST-1' else 820),
                                   8, diagnostics=diagnostics)
                    wait_size(large)
                    closed_at = time.monotonic()
                    for name in rest_before:
                        send(f'out {name}')
                    rest_large = 284 if style == 'fan' else 473
                    wait_size(rest_large, 14 * factor + (.65 if factor < 1 else 0), width=216)
                    shrink = [stamp for _, stamp, item in records()
                              if stamp > closed_at and item[9] == 216]
                    assert shrink and min(shrink) >= closed_at + 10 * factor, shrink
                configured = {}
                for line in server_log.read_text().splitlines():
                    fields = line.split()
                    if fields[:1] == ['tier-configure']:
                        configured.setdefault((fields[1], int(fields[3]), int(fields[4])),
                                              float(fields[2]))
                for name, stamp, item in records()[start:]:
                    w, h, x0, y0, x1, y1, mt, mb, ml, sw, sh, cx, cy, region_count = item
                    assert configured[(name, sw, sh)] <= stamp, (name, stamp, item)
                    assert region_count >= 1, item
                    assert 0 <= x0 <= x1 < w and 0 <= y0 <= y1 < h, item
                    assert w == (sw * scale + 119) // 120, item
                    assert h == (sh * scale + 119) // 120, item
                    # Visible content must keep a transparent vertical border.
                    # Cat at the output edge is allowed to touch that edge.
                    output_h = 600 if name == 'TEST-1' else 768
                    origin = mt if top else output_h - mb - sh
                    old = original[name]
                    old_origin = old[6] if top else output_h - old[7] - old[10]
                    assert ml + cx == old[8] + old[11], (name, item, old)
                    assert origin + cy == old_origin + old[12], (name, item, old)
                    assert ((origin * scale) // 120 + (cy * scale) // 120 ==
                            (old_origin * scale) // 120 + (old[12] * scale) // 120), item
                    if origin > 0:
                        assert y0 > 0, item
                    if origin + sh < output_h:
                        assert y1 < h - 1, item
                committed_margins = {}
                for line in server_log.read_text().splitlines():
                    fields = line.split()
                    if fields[:1] == ['tier-submit']:
                        values = tuple(map(int, fields[3:]))
                        committed_margins[fields[1]] = values[6:9]
                    elif fields[:1] == ['tier-size-only'] and fields[1] in committed_margins:
                        assert tuple(map(int, fields[2:])) == committed_margins[fields[1]], line
                assert 'AddressSanitizer' not in app_log.read_text()
                assert 'runtime error:' not in app_log.read_text()
                print(f'Surface tiers {style} {"top" if top else "bottom"} '
                      f'{scale}/120 passed: 0 -> 1 -> 6 -> 1 -> 0 '
                      f'(shrink delay {10 * factor:g}s)')
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


def main(real_time=False):
    run('fan', False, 120, clamped=True, real_time=real_time)
    for style in ('fan', 'post'):
        for top in (False, True):
            for scale in (120, 240, 150):
                run(style, top, scale,
                    real_time=real_time or (style, top, scale) == ('fan', False, 120))
    print('Surface tier runtime matrix passed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--real-time', action='store_true')
    main(parser.parse_args().real_time)
