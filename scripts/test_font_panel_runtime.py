#!/usr/bin/env python3
"""Open and destroy the font panel on the isolated compositor fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())


def wait_for(condition):
    end = time.monotonic() + 8
    while time.monotonic() < end:
        if condition():
            return
        time.sleep(.03)
    raise AssertionError('font panel fixture condition timed out')


def name_center(rect):
    # The font name sits between the arrows, at the same fraction of the card
    # at every cat scale. 77/154 and 103/168 are the scale-1 center (four rows).
    return (rect[0] + rect[2] * 77 // 154, rect[1] + rect[3] * 103 // 168)


with tempfile.TemporaryDirectory(prefix='herdcat-font-panel-') as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
               WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
               PYTHONDONTWRITEBYTECODE='1')
    config = root / 'cat.conf'
    config.write_text(
        'monitor=TEST-1,TEST-2\noverlay_opacity=0\noverlay_position=bottom\n'
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

    def check_anchor(output, card, top, output_size):
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
        # The card region is an integer cover of a card that is still easing
        # in when it is sampled, and the panel follows the card as it settles,
        # so the two differ by a few pixels. A missing surface margin would be
        # off by hundreds.
        y = max(0, min(card_y + card[3] - panel[5], output_size[1] - panel[5]))
        assert abs(panel[3] - x) <= 4, (main, panel, card, x)
        assert abs(panel[0] - y) <= 6, (main, panel, card, y)

    def card_region(output, cat):
        # A right click on the cat first shows the hover area around the cat,
        # then the card above it. Taking the first changed region clicked the
        # cat instead of the font name whenever the hover frame was sampled.
        def above():
            rect = regions().get(output)
            return bool(rect) and rect != cat and rect[1] + rect[3] <= cat[1] + 2
        wait_for(above)
        time.sleep(.35)
        wait_for(above)
        return regions()[output]

    def click(output, rect, button):
        send(f'tap {output} {rect[0]} {rect[1]} {button}')

    try:
        wait_for(lambda: (root / 'wayland-test').exists())
        app = start()
        wait_for(lambda: len(regions()) == 2 and all(v[2] for v in regions().values()))
        cat = regions()['TEST-1']
        click('TEST-1', (cat[0] + cat[2] // 2, cat[1] + cat[3] // 2), 273)
        card = card_region('TEST-1', cat)
        name = name_center(card)
        click('TEST-1', name, 272)
        wait_for(lambda: 'overlay TEST-1 herdcat-font-panel' in text())
        wait_for(lambda: 'commit TEST-1' in text().rsplit('herdcat-font-panel', 1)[-1])
        check_anchor('TEST-1', card, False, (800, 600))
        # The panel follows the card while it eases in; sample once it rests.
        def settled():
            first = placements()[('TEST-1', 'herdcat-font-panel')]
            time.sleep(.3)
            return first == placements()[('TEST-1', 'herdcat-font-panel')]
        wait_for(settled)
        panel_x = placements()[('TEST-1', 'herdcat-font-panel')][3]
        send('step')  # Narrower output and new fractional scale while browsing.
        wait_for(lambda: 'phase 1' in text())
        wait_for(lambda: placements()[('TEST-1', 'herdcat-overlay')][4] == 238)
        time.sleep(.2)
        assert abs(placements()[('TEST-1', 'herdcat-font-panel')][3] - panel_x) <= 1
        # The card steps aside while the panel is open, so there is no font
        # name to click again. A right click on the cat closes the menu and
        # the panel with it.
        middle = (cat[0] + cat[2] // 2, cat[1] + cat[3] // 2)
        click('TEST-1', middle, 273)
        wait_for(lambda: 'gone herdcat-font-panel' in text())
        time.sleep(.4)

        click('TEST-1', middle, 273)
        name = name_center(card_region('TEST-1', cat))
        click('TEST-1', name, 272)
        wait_for(lambda: text().count('overlay TEST-1 herdcat-font-panel') >= 2)
        command('hide')
        wait_for(lambda: text().count('gone herdcat-font-panel') >= 2)
        command('show')
        wait_for(lambda: regions().get('TEST-2', (0, 0, 0, 0))[2])

        cat = regions()['TEST-2']
        click('TEST-2', (cat[0] + cat[2] // 2, cat[1] + cat[3] // 2), 273)
        card = card_region('TEST-2', cat)
        name = name_center(card)
        click('TEST-2', name, 272)
        wait_for(lambda: 'overlay TEST-2 herdcat-font-panel' in text())
        check_anchor('TEST-2', card, True, (1024, 768))
        send('step')
        wait_for(lambda: 'phase 2' in text())
        wait_for(lambda: text().count('gone herdcat-font-panel') >= 3)
        time.sleep(.4)
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
        server.stdin.close()
        server_file.close()
        app_file.close()
