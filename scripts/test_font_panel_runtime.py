#!/usr/bin/env python3
"""Open and destroy the font panel on the isolated compositor fixture."""
import os
import socket
from pathlib import Path
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_settled, wait_until

binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())


def wait_for(condition, seconds=8):
    return wait_until(condition, seconds, description='test_font_panel_runtime.py condition',
                      diagnostics=lambda: server_log.read_text()[-4000:] + app_log.read_text()[-2000:])


def name_center(rect, below=False):
    # The font name sits between the arrows, at the same fraction of the card
    # at every cat scale. 77/154 and 103/168 are the scale-1 center (four rows).
    row = 168 - 103 if below else 103
    return (rect[0] + rect[2] * 77 // 154, rect[1] + rect[3] * row // 168)


with tempfile.TemporaryDirectory(prefix='herdcat-font-panel-') as directory:
    root = Path(directory)
    env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
               WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
               PYTHONDONTWRITEBYTECODE='1')
    config = root / 'cat.conf'
    config.write_text(
        'keyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
            'hotplug_scan_interval=0\nmonitor=TEST-1,TEST-2\noverlay_opacity=0\noverlay_position=bottom\n'
        'disable_fullscreen_hide=1\ncat_x_offset=0\ncat_y_offset=0\n'
        'sign_style=fan\n[monitor:TEST-2]\noverlay_position=top\n[global]\n')
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

    def text():
        return server_log.read_text()

    def regions():
        result = {}
        for line in text().splitlines():
            parts = line.split()
            if parts and parts[0] == 'input':
                result[parts[1]] = tuple(map(int, parts[2:]))
        return result

    def placements():
        result = {}
        for line in text().splitlines():
            parts = line.split()
            if parts[:1] == ['placement']:
                result[(parts[1], parts[2])] = tuple(map(int, parts[3:]))
        return result

    def cat_regions():
        result = {}
        for line in text().splitlines():
            parts = line.split()
            if parts[:1] == ['snapshot']:
                result[parts[1]] = tuple(map(int, parts[7:11]))
        return result

    def check_anchor(output, card, top, output_size, below=False):
        wait_settled(placements, ready=lambda p: (output, 'herdcat-font-panel') in p,
                     description=f'{output} panel anchors',
                     diagnostics=lambda: text()[-5000:])
        main = placements()[(output, 'herdcat-overlay')]
        wait_for(lambda: (output, 'herdcat-font-panel') in placements())
        panel = placements()[(output, 'herdcat-font-panel')]
        # The card is surface-local, the independent panel uses output pixels.
        card_x = main[3] + card[0]
        card_y = (main[0] if top else output_size[1] - main[2] - main[5]) + card[1]
        scale = 40 / 110
        panel_w = 384 * scale
        gap = 10 * scale
        x = card_x + card[2] + gap
        if x + panel_w > output_size[0]:
            x = card_x - gap - panel_w
        x = max(0, min(x, output_size[0] - panel_w))
        # Both rectangles are sampled after transitions settle. Their integer
        # covers and the panel's rounded margin can differ by one pixel.
        y = max(0, min(card_y + card[3] - panel[5], output_size[1] - panel[5]))
        assert abs(panel[3] - x) <= 1, (main, panel, card, x)
        assert abs(panel[0] - y) <= 1, (main, panel, card, y)

    def card_region(output, below=False):
        # A right click on the cat first shows the hover area around the cat,
        # then the card above it. Taking the first changed region clicked the
        # cat instead of the font name whenever the hover frame was sampled.
        def visible_card():
            rect = regions().get(output)
            cat = cat_regions().get(output)
            return (bool(rect) and bool(cat) and rect != cat and
                    (rect[1] >= cat[1] + cat[3] - 2 if below else
                     rect[1] + rect[3] <= cat[1] + 2))
        wait_for(visible_card)
        settled = wait_settled(regions, ready=lambda _: visible_card(),
                               description=f'{output} switch card',
                               diagnostics=lambda: text()[-5000:])
        return settled[output]

    def click(output, rect, button):
        send(f'tap {output} {rect[0]} {rect[1]} {button}')

    def click_cat(output):
        cat = cat_regions()[output]
        click(output, (cat[0] + cat[2] // 2, cat[1] + cat[3] // 2), 273)

    try:
        wait_for(lambda: (root / 'wayland-test').exists())
        app = start()
        wait_for(lambda: len(regions()) == 2 and all(v[2] for v in regions().values()))
        wait_settled(regions, ready=lambda p: len(p) == 2 and all(v[2] for v in p.values()),
                     diagnostics=lambda: text()[-5000:])
        click_cat('TEST-1')
        card = card_region('TEST-1')
        name = name_center(card)
        click('TEST-1', name, 272)
        wait_for(lambda: 'overlay TEST-1 herdcat-font-panel' in text())
        wait_for(lambda: 'commit TEST-1' in text().rsplit('herdcat-font-panel', 1)[-1])
        check_anchor('TEST-1', card, False, (800, 600))
        # Hiding the card must not move the panel's opening output anchor.
        wait_settled(placements, description='font panel placement',
                     diagnostics=lambda: text()[-5000:])
        opened = [tuple(map(int, line.split()[3:])) for line in text().splitlines()
                  if line.startswith('placement TEST-1 herdcat-font-panel ')]
        assert opened and all(p == opened[0] for p in opened), opened
        panel_x = placements()[('TEST-1', 'herdcat-font-panel')][3]
        send('step')  # Narrower output and new fractional scale while browsing.
        wait_for(lambda: 'phase 1' in text())
        wait_for(lambda: placements()[('TEST-1', 'herdcat-overlay')][4] == 238)
        wait_settled(placements, diagnostics=lambda: text()[-5000:])
        assert abs(placements()[('TEST-1', 'herdcat-font-panel')][3] - panel_x) <= 1
        # The card steps aside while the panel is open, so there is no font
        # name to click again. A right click on the cat closes the menu and
        # the panel with it.
        click_cat('TEST-1')
        wait_for(lambda: 'gone herdcat-font-panel' in text())
        wait_settled(regions, diagnostics=lambda: text()[-5000:])

        # The signs still face above at y=50 (40px cat), but the card
        # needs 66px above. Its independent below anchor must be honored.
        main = placements()[('TEST-1', 'herdcat-overlay')]
        original_cat_y = 600 - main[2] - main[5] + cat_regions()['TEST-1'][1]
        send(f'drag TEST-1 0 {50 - original_cat_y}')
        wait_settled(placements, diagnostics=lambda: text()[-5000:])
        send('out TEST-1')
        wait_settled(regions, diagnostics=lambda: text()[-5000:])
        assert cat_regions()['TEST-1'][1] > 3  # signs remain above
        click_cat('TEST-1')
        card = card_region('TEST-1', below=True)
        click('TEST-1', name_center(card, below=True), 272)
        wait_for(lambda: text().count('overlay TEST-1 herdcat-font-panel') >= 2)
        check_anchor('TEST-1', card, False, (640, 600), below=True)
        anchor_before = placements()[('TEST-1', 'herdcat-font-panel')]
        for key in range(1, 7):
            with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                control.connect(str(root / 'herdcat.sock'))
                control.sendall(f'ev claude working {key:016x} 0'.encode())
                assert control.recv(512).startswith(b'0 ')
        wait_for(lambda: placements()[('TEST-1', 'herdcat-overlay')][5] == 219)
        wait_settled(placements, diagnostics=lambda: text()[-5000:])
        assert placements()[('TEST-1', 'herdcat-font-panel')] == anchor_before
        for key in range(1, 7):
            with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                control.connect(str(root / 'herdcat.sock'))
                control.sendall(f'ev claude end {key:016x} 0'.encode())
                assert control.recv(512).startswith(b'0 ')
        click_cat('TEST-1')
        wait_for(lambda: text().count('gone herdcat-font-panel') >= 2)
        wait_settled(regions, diagnostics=lambda: text()[-5000:])
        send(f'drag TEST-1 0 {original_cat_y - 50}')
        wait_settled(placements, diagnostics=lambda: text()[-5000:])

        click_cat('TEST-1')
        name = name_center(card_region('TEST-1'))
        click('TEST-1', name, 272)
        wait_for(lambda: text().count('overlay TEST-1 herdcat-font-panel') >= 3)
        command('hide')
        wait_for(lambda: text().count('gone herdcat-font-panel') >= 3)
        command('show')
        wait_for(lambda: regions().get('TEST-2', (0, 0, 0, 0))[2])

        wait_settled(regions, ready=lambda p: p.get('TEST-2', (0, 0, 0, 0))[2],
                     diagnostics=lambda: text()[-5000:])
        click_cat('TEST-2')
        card = card_region('TEST-2', below=True)
        name = name_center(card, below=True)
        click('TEST-2', name, 272)
        wait_for(lambda: 'overlay TEST-2 herdcat-font-panel' in text())
        check_anchor('TEST-2', card, True, (1024, 768), below=True)
        send('step')
        wait_for(lambda: 'phase 2' in text())
        wait_for(lambda: text().count('gone herdcat-font-panel') >= 4)
        wait_settled(regions, diagnostics=lambda: text()[-5000:])
        app.terminate()
        assert app.wait(timeout=3) == 0
        log = app_log.read_text()
        assert 'ERROR: AddressSanitizer' not in log
        assert 'runtime error:' not in log
        print('font panel: create, close, hide and output removal passed.')
    except Exception:
        print(text()[-7000:])
        print(app_log.read_text()[-4000:])
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
        server_file.close()
        app_file.close()
