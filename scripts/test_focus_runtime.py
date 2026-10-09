#!/usr/bin/env python3
"""A delayed Wayland release must not postpone quiet-output sampling."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

from runtime_test_helpers import control_ready, runtime_env, wait_settled, wait_until

from measure_focus_fixture import FocusFixture
from measure_scenarios import CONFIG, stop, wait_for, wire

root = Path(__file__).resolve().parent.parent
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/herdcat'
with tempfile.TemporaryDirectory(prefix='herdcat-focus-runtime-') as temporary:
    directory = Path(temporary)
    focus = FocusFixture(directory, 1, title='✳ Claude Code').start()
    env = runtime_env(XDG_RUNTIME_DIR=temporary, XDG_STATE_HOME=temporary,
               WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_MEASURE='1',
               HERDCAT_TEST_RELEASE_MS='1000', NIRI_SOCKET=str(focus.path))
    for key in ('HYPRLAND_INSTANCE_SIGNATURE', 'HERDCAT_TEST_DRAG', 'WAYLAND_DEBUG'):
        env.pop(key, None)
    config = directory / 'test.conf'
    # Keep the long rescan interval: with none the helper exits when it finds
    # no keyboard, and this test needs it alive.
    config.write_text(CONFIG.replace('sign_animations=full', 'sign_animations=off'))
    server = app = None
    helper = None
    try:
        with (directory / 'runtime.log').open('w') as log:
            server = subprocess.Popen([str(root / 'build/compositor/server')], env=env,
                                      stdout=log, stderr=log)
            wait_for(lambda: (directory / 'wayland-test').exists(), [server])
            app = subprocess.Popen([str(binary), '-c', str(config)], cwd=directory,
                                   env=env, stdout=log, stderr=log)
            wait_for(lambda: control_ready(directory / 'herdcat.sock'), [server, app])
            wait_for(lambda: 'commit TEST-1' in (directory / 'runtime.log').read_text(),
                     [server, app])
            helpers = Path(f'/proc/{app.pid}/task/{app.pid}/children').read_text().split()
            assert len(helpers) == 1, helpers
            helper = int(helpers[0])
            assert b'--input-helper' in Path(f'/proc/{helper}/cmdline').read_bytes().split(b'\0')
            os.kill(helper, signal.SIGSTOP)
            wait_settled(lambda: (directory / 'runtime.log').read_text(),
                         minimum=2, description='startup buffer release',
                         diagnostics=lambda: (directory / 'runtime.log').read_text()[-4000:])
            wire(directory, f'ev claude working aaaaaaaaaaaaaaaa {focus.agent_pid}')
            assert 'agent=working' in wire(directory, 'status')
            # No control requests during the grace interval: they would refresh
            # the runtime timeout and hide a deadline postponed by releases.
            time.sleep(2.35)
            assert 'agent=idle' in wire(directory, 'status')
            print('quiet output: two 1000 ms windows survived 1000 ms releases with animation off')
    finally:
        if helper is not None and Path(f'/proc/{helper}').exists():
            os.kill(helper, signal.SIGCONT)
        stop(app)
        stop(server)
        focus.close()
