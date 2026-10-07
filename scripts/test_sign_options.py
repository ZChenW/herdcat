#!/usr/bin/env python3
"""Exercise sign reloads and hook replay on an isolated compositor."""
import os
import socket
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_settled, wait_until
from pathlib import Path
temporary = tempfile.TemporaryDirectory(prefix='herdcat-sign-options-')
r = Path(temporary.name)
env = runtime_env(XDG_RUNTIME_DIR=str(r), XDG_STATE_HOME=str(r),
           WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_DRAG='1',
           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1')
env.pop('NIRI_SOCKET', None)
binary = str(Path('build/herdcat').resolve())
base = ('keyboard_device=/dev/input/herdcat-runtime-nonexistent\nmonitor=TEST-1\noverlay_position=bottom\ncat_height=110\n'
        'overlay_height=120\ndisable_fullscreen_hide=1\n'
        'hotplug_scan_interval=0\nagent_stale_timeout=0\n')
config = r / 'test.conf'
config.write_text(base)
server_log = (r / 'server.log').open('w')
app_log = (r / 'app.log').open('w')
server = subprocess.Popen(['build/compositor/server'], env=env,
                          stdin=subprocess.PIPE, stdout=server_log,
                          stderr=server_log)
app = None

def wait(condition, seconds=6):
    return wait_until(condition, seconds, description='test_sign_options.py condition',
                      diagnostics=lambda: (r / "server.log").read_text()[-4000:] + (r / "app.log").read_text()[-2000:])


def wire(text, ok=True):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(3)
        s.connect(str(r / 'herdcat.sock'))
        s.sendall(text.encode())
        out = s.recv(512).decode()
    assert out.startswith('0 ') == ok, (text, out)
    return out
def geometry():
    latest = {}
    text = (r / 'server.log').read_text()
    # The fixture may be in the middle of writing its last line.
    for line in text[:text.rfind('\n') + 1].splitlines():
        fields = line.split()
        if len(fields) > 2 and fields[0] in ('placement', 'input'):
            key = (fields[0], fields[1], fields[2] if fields[0] == 'placement' else '')
            latest[key] = tuple(fields[2:])
    return latest


def settled():
    # A waiting sign sways for as long as it waits, so its input region never
    # rests. The surface placement does; that is what a reload can change.
    def placement():
        return {key: value for key, value in geometry().items()
                if key[0] == 'placement'}
    return wait_settled(placement, ready=bool, description='sign geometry',
                        diagnostics=lambda: (r / 'server.log').read_text()[-4000:])

try:
    wait(lambda: (r / 'wayland-test').exists())
    app = subprocess.Popen([binary, '-c', str(config), '-w'], env=env,
                           stdout=app_log, stderr=app_log)
    wait(lambda: (r / 'herdcat.sock').exists())
    wire('ev claude waiting aaaaaaaaaaaaaaaa 0')
    wire('name aaaaaaaaaaaaaaaa 演示 project')
    wire('ask aaaaaaaaaaaaaaaa SIGNS_PRIVATE_TITLE')
    values = {
        'sign_style': ['fan', 'post', 'off'],
        'sign_max': list(range(1, 11)),
        'sign_idle': ['hover', 'always', 'never'],
        'sign_font': ['', 'Noto Sans', 'monospace', 'missing-herdcat-font'],
        'sign_font_size': list(range(10, 21)),
        'sign_animations': ['full', 'reduced', 'off'],
        'sign_theme': ['light', 'dark', 'auto'],
        'sign_language': ['auto', 'en', 'zh'],
        'sign_done': ['sticky', 'timeout'],
        'sign_typing_desk': [0, 1],
        'sign_name': ['project', 'title', 'auto'],
        'sign_name_extra': ['inline', 'off', 'end', 'above', 'below'],
        'sign_title_length': [0, 8, 16, 64],
        'sign_nameplate': ['', '**{name}**  {title} · {agent} · {state}',
                           '**{name}**  {agent} · {state}\\n{title}'],
    }
    for key, choices in values.items():
        for value in choices:
            config.write_text(base + f'{key}={value}\n')
            wire('reload')
            settled()
            assert 'waiting' in wire('sessions')
        print('live reload', key, choices, flush=True)
    config.write_text(base + 'sign_done=sticky\nagent_done_timeout=1\n')
    wire('reload')
    wire('ev claude done aaaaaaaaaaaaaaaa 0')
    time.sleep(1.2)
    assert 'unread' in wire('sessions')
    config.write_text(base + 'sign_done=timeout\nagent_done_timeout=1\n')
    wire('reload')
    assert 'unread' not in wire('sessions')
    wait(lambda: ' idle ' in wire('sessions'))
    config.write_text(base + 'sign_done=sticky\nagent_done_timeout=1\n')
    wire('reload')
    wire('ev claude done aaaaaaaaaaaaaaaa 0')
    time.sleep(1.2)
    assert 'unread' in wire('sessions')
    config.write_text(base + 'sign_done=invalid\n')
    wire('reload', False)
    assert 'unread' in wire('sessions')
    wire('ev claude end aaaaaaaaaaaaaaaa 0')
    # The reserved manual session (key zero) also obeys reloaded done policy.
    wire('state done')
    assert 'unread' in wire('sessions')
    config.write_text(base + 'sign_done=timeout\nagent_done_timeout=1\n')
    wire('reload')
    assert 'unread' not in wire('sessions')
    wait(lambda: 'agent=idle' in wire('status'))
    wire('state idle')

    def counters():
        stats = Path(f'/proc/{app.pid}/stat').read_text().split(') ', 1)[1].split()
        status = Path(f'/proc/{app.pid}/status').read_text().splitlines()
        switches = next(x.split()[1] for x in status
                        if x.startswith('voluntary_ctxt_switches:'))
        return [int(stats[11]) + int(stats[12]), int(switches)]
    idle = {}
    for style in ['fan', 'post', 'off']:
        config.write_text(base + f'sign_style={style}\n')
        wire('reload')
        settled()
        before = counters()
        time.sleep(1.2)
        after = counters()
        idle[style] = [b - a for a, b in zip(before, after)]
        # Observational only: hardware input and helper recovery may wake
        # the process independently of signs. Model tests assert no deadlines.
    print('idle CPU ticks/context-switch deltas over 1.2s:', idle, flush=True)
    subprocess.run(['scripts/replay_agent_hooks.sh', '--config', str(config),
                    '--pid', str(app.pid)], env=dict(env, HERDCAT_BIN=binary),
                   check=True, timeout=35)
    assert app.wait(timeout=5) == 0
    log = (r / 'app.log').read_text()
    assert 'AddressSanitizer' not in log and 'runtime error:' not in log
    assert 'SIGNS_PRIVATE_TITLE' not in log
    print('Sign reload matrix, sticky/timeout, invalid reload and replay passed.')
finally:
    if app and app.poll() is None:
        app.terminate()
        app.wait(timeout=5)
    server.terminate()
    server.wait(timeout=3)
    if app and app.returncode:
        print((r / 'app.log').read_text())
    app_log.close()
    server_log.close()
    temporary.cleanup()
