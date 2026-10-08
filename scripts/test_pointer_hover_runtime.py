#!/usr/bin/env python3
"""Session recovery and pointer handling production control/Wayland regression; requires Unix sockets."""
from pathlib import Path
import os
import socket
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_until

binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())
with tempfile.TemporaryDirectory(prefix='herdcat-pointer-hover-runtime-') as temporary:
    root = Path(temporary)
    env = runtime_env(XDG_RUNTIME_DIR=temporary, XDG_STATE_HOME=temporary,
                      HOME=temporary, WAYLAND_DISPLAY='wayland-test',
                      HERDCAT_TEST_DRAG='1', HERDCAT_HOOK_DEBUG='1')
    env.pop('NIRI_SOCKET', None)
    config = root / 'cat.conf'
    config.write_text('monitor=TEST-1\nkeyboard_device=/dev/input/herdcat-runtime-nonexistent\n'
                      'cat_height=110\noverlay_height=120\noverlay_opacity=0\n'
                      'overlay_position=top\n'
                      'sign_style=post\nsign_animations=off\nsign_idle=always\n'
                      'agent_interrupt_detect=0\nagent_stale_timeout=0\n'
                      'disable_fullscreen_hide=1\nenable_debug=1\n')
    transcript = root / 'transcript.jsonl'
    private = 'synthetic private first prompt'
    transcript.write_text('{"type":"user","isMeta":false,"message":'
                          '{"role":"user","content":"' + private + '\\nsecond line"}}\n')
    server_log, app_log = root / 'server.log', root / 'app.log'

    def wire(message):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
            control.settimeout(3)
            control.connect(str(root / 'herdcat.sock'))
            control.sendall(message.encode())
            reply = control.recv(4096).decode()
            assert reply.startswith('0 '), 'control failed'
            return reply

    with server_log.open('w') as sl, app_log.open('w') as al:
        server = subprocess.Popen([fixture], env=env, stdin=subprocess.PIPE,
                                  stdout=sl, stderr=sl, text=True)
        app = None
        try:
            wait_until(lambda: (root / 'wayland-test').exists(), 5)
            app = subprocess.Popen([binary, '-c', str(config)], env=env,
                                   stdout=al, stderr=al)
            wait_until(lambda: (root / 'herdcat.sock').exists(), 5)
            wire('ev claude start 1111111100000000 0')
            wire('cwd 1111111100000000 ' + b'/work/Projects'.hex() + ' Projects')
            wire('cwd 1111111100000000 ' + b'/work/Projects/herdcat'.hex() + ' herdcat')
            wire('cwd 1111111100000000 ' + b'/work/Projects'.hex() + ' wrong')
            assert ' herdcat' in wire('sessions') and ' wrong' not in wire('sessions')
            wire(f'path 1111111100000000 {transcript}')
            wait_until(lambda: 'title~=' + private in wire('sessions'), 5)
            wire('ttl 1111111100000000 explicit title')
            wire(f'path 1111111100000000 {transcript}')
            assert 'title=explicit title' in wire('sessions')
            # The second input rectangle is the first sign, after the cat.
            def plate():
                rows = [tuple(map(int, line.split()[2:]))
                        for line in server_log.read_text().splitlines()
                        if line.startswith('sign-input TEST-1 ')]
                return rows[-1] if rows else (0, 0, 0, 0)
            def snapshot():
                rows = [tuple(map(int, line.split()[2:]))
                        for line in server_log.read_text().splitlines()
                        if line.startswith('snapshot TEST-1 ')]
                return rows[-1] if rows else None
            wait_until(lambda: plate()[2] > 0, 5)
            resting_surface = snapshot()
            assert resting_surface is not None
            x, y, w, h = plate()
            # Collapsed Claude now exposes only the 34x27 fan state face;
            # its half-pixel placement rounds the input height out to 28.
            assert (w, h) == (34, 28), plate()
            rest_x, rest_y = x, y
            x, y = x + w // 2, y + h // 2
            server.stdin.write(f'hover TEST-1 {x} {y}\n')
            server.stdin.flush()
            wait_until(lambda: plate()[2] >= 150, 5)
            # Expanded targets include the 6px gap and the resting edge.
            expanded = plate()
            expanded_surface = snapshot()
            # Growth moves the surface origin, while cat/sign output positions
            # stay fixed. The fixture's later motions use the new surface.
            assert (resting_surface[2] + resting_surface[5] ==
                    expanded_surface[2] + expanded_surface[5])
            assert (resting_surface[0] + resting_surface[6] ==
                    expanded_surface[0] + expanded_surface[6])
            dx = expanded_surface[5] - resting_surface[5]
            dy = expanded_surface[6] - resting_surface[6]
            rest_x, rest_y = rest_x + dx, rest_y + dy
            y += dy
            assert expanded[0] <= rest_x < expanded[0] + expanded[2], expanded
            assert expanded[1] <= rest_y + h // 2 < expanded[1] + expanded[3]
            x = rest_x + 5 + 34 + 3
            server.stdin.write(f'motion TEST-1 {x} {y}\n')
            server.stdin.flush()
            time.sleep(.4)
            assert plate()[2] >= 150, plate()
            before = server_log.read_text().count('commit TEST-1 ')
            for i in range(1000):
                server.stdin.write(f'motion TEST-1 {x + i % 2} {y}\n')
                server.stdin.flush()
                time.sleep(.001)
            server.stdin.write('motion-stats\n')
            server.stdin.flush()
            wait_until(lambda: 'motion-count 1001' in server_log.read_text(), 5)
            time.sleep(.1)
            assert server_log.read_text().count('commit TEST-1 ') == before
            assert private not in app_log.read_text()
            assert 'explicit title' not in app_log.read_text()
            # Flush/restart preserves start cwd and the explicit title.
            app.terminate()
            assert app.wait(timeout=3) == 0
            app = subprocess.Popen([binary, '-c', str(config)], env=env,
                                   stdout=al, stderr=al)
            wait_until(lambda: (root / 'herdcat.sock').exists(), 5)
            wire('cwd 1111111100000000 ' + b'/work/Projects'.hex() + ' wrong')
            assert ' herdcat' in wire('sessions')
            assert 'title=explicit title' in wire('sessions')
            print('Session recovery and pointer handling cwd restart, asynchronous title/privacy and 1000 motions passed.')
        finally:
            for process in (app, server):
                if process and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=3)
