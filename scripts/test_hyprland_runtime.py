#!/usr/bin/env python3
"""Opt-in real Hyprland acceptance inside a host Wayland window.

One launch per invocation, ended by a PID watchdog. HERDCAT_HYPRLAND_BUDGET=N
caps the launches recorded in the /tmp ledger, for unattended runs.
Only Hyprland receives the host display. Clients and herdcat use private sockets.
"""
import argparse
from collections import deque
import fcntl
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

from test_sway_runtime import SwayTest, ROOT, BINARY, CLIENT, KEYS, layer_roundtrip
from runtime_test_helpers import control_ready

def isolated_env(directory):
    return dict(PATH='/usr/bin:/bin', LANG='C.UTF-8', HOME=str(directory),
                XDG_RUNTIME_DIR=str(directory),
                XDG_CONFIG_HOME=str(directory / 'config'),
                XDG_CONFIG_DIRS=str(directory / 'config'),
                XDG_CACHE_HOME=str(directory / 'cache'),
                XDG_STATE_HOME=str(directory / 'state'),
                XDG_DATA_HOME=str(directory / 'data'),
                PYTHONDONTWRITEBYTECODE='1')


def host_display():
    display = os.environ.get('WAYLAND_DISPLAY')
    runtime = os.environ.get('XDG_RUNTIME_DIR')
    if not display or (not display.startswith('/') and not runtime):
        return None
    path = Path(display) if display.startswith('/') else Path(runtime) / display
    return path if path.is_socket() else None


class HyprlandTest(SwayTest):
    def __init__(self, directory, host, evidence):
        self.directory, self.host, self.evidence = directory, host, evidence
        self.env = isolated_env(directory)
        self.processes, self.logs = [], {}
        self.history = deque(maxlen=16)
        self.hypr = self.app = None
        self.watchdogs = []
        self.launch = None
        digest = hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]
        self.ledger = Path('/tmp') / f'hc-hypr-budget-{digest}.json'

    def ipc(self, *args):
        self.check('HYPRLAND_INSTANCE_SIGNATURE' in self.env,
                   'private Hyprland signature not yet known')
        return json.loads(self.run(['/usr/bin/hyprctl', '-j', *args]).stdout)

    def dispatch(self, command, argument):
        expression = (f'hl.dsp.focus({{window="{argument}"}})' if command == 'focuswindow'
                      else f'hl.dsp.window.close({{window="{argument}"}})')
        result = self.run(['/usr/bin/hyprctl', 'dispatch', expression])
        self.check(result.stdout.strip() == 'ok', f'dispatch failed: {result.stdout}')

    def focused(self):
        return int(self.ipc('activewindow').get('address', '0x0'), 16)

    def focus(self, window, key, other_key, other_window):
        self.dispatch('focuswindow', f'address:0x{window:x}')
        self.wait('Hyprland focus command', lambda: self.focused() == window)
        self.wait('herdcat focus event', lambda:
                  self.mapped(key, window, 'yes') and
                  self.mapped(other_key, other_window, 'no'))

    def overlay_present(self):
        layers = self.ipc('layers')
        return any(layer.get('namespace') == 'herdcat-overlay'
                   for monitor in layers.values()
                   for rows in monitor.get('levels', {}).values()
                   for layer in rows)

    def start_nested(self):
        # Lock and persist the reservation before launching, even on failure.
        with self.ledger.open('a+') as ledger:
            fcntl.flock(ledger, fcntl.LOCK_EX)
            ledger.seek(0)
            launches = json.loads(ledger.read() or '[]')
            budget = int(os.environ.get('HERDCAT_HYPRLAND_BUDGET', '0'))
            self.check(not budget or len(launches) < budget,
                       f'nested launch budget exhausted ({budget})')
            self.launch = dict(number=len(launches) + 1, started=time.time())
            launches.append(self.launch)
            ledger.seek(0)
            ledger.truncate()
            json.dump(launches, ledger)
        config = self.directory / 'minimal.lua'
        config.write_text(
            'hl.monitor({ output = "", mode = "1280x720@60", '
            'position = "0x0", scale = 1 })\n'
            'hl.config({ xwayland = { enabled = false }, '
            'debug = { enable_stdout_logs = true, disable_logs = false }, '
            'misc = { disable_hyprland_logo = true, '
            'disable_splash_rendering = true }, '
            'input = { follow_mouse = 0 } })\n')
        env = dict(self.env, WAYLAND_DISPLAY=str(self.host),
                   LIBSEAT_BACKEND='seatd',
                   SEATD_SOCK=str(self.directory / 'no-seat.sock'),
                   AQ_DRM_DEVICES=str(self.directory / 'no-gpu'),
                   WAYLAND_DEBUG='client')
        for key in ('NO_RT', 'NO_CRASHREPORTER', 'NO_SD_VARS', 'NO_SD_NOTIFY',
                    'NO_SD_TARGET'):
            env['HYPRLAND_' + key] = '1'
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        self.started = time.monotonic()
        self.hypr = self.spawn('hyprland', ['/usr/bin/Hyprland', '--config', str(config)],
                               env=env)
        self.launch['pid'] = self.hypr.pid
        # Independent of test waits/IPC/diagnostics: TERM at 35s, KILL at 38s.
        for delay, sig in ((35, signal.SIGTERM), (38, signal.SIGKILL)):
            timer = threading.Timer(delay, self.stop_nested, args=(sig,))
            timer.daemon = True
            timer.start()
            self.watchdogs.append(timer)
        print(f'Nested launch {self.launch["number"]}: PID {self.hypr.pid}', flush=True)

    def stop_nested(self, sig=signal.SIGTERM):
        if self.hypr and self.hypr.poll() is None:
            try:
                self.hypr.send_signal(sig)
            except ProcessLookupError:
                pass

    def execute(self):
        self.run(['make', 'all', 'sway-runtime-build'], timeout=180)
        for key in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME',
                    'XDG_DATA_HOME'):
            Path(self.env[key]).mkdir(exist_ok=True)
        self.start_nested()

        def private_ready():
            paths = list((self.directory / 'hypr').glob('*/.socket2.sock'))
            if len(paths) != 1 or not (paths[0].parent / '.socket.sock').is_socket():
                return False
            self.env['HYPRLAND_INSTANCE_SIGNATURE'] = paths[0].parent.name
            sockets = [p for p in self.directory.glob('wayland-*') if p.is_socket()]
            if len(sockets) != 1:
                return False
            self.env['WAYLAND_DISPLAY'] = sockets[0].name
            monitors = self.ipc('monitors')
            return len(monitors) == 1 and monitors[0]['name'] == 'WAYLAND-1'

        self.wait('private Hyprland IPC/Wayland/monitor', private_ready)
        clients = [self.spawn(f'client-{i}', [str(CLIENT), f'Hyprland fixture {i}'],
                              stdin=subprocess.PIPE) for i in range(2)]
        windows = {}

        def discover():
            windows.clear()
            windows.update({node['pid']: int(node['address'], 16) for node in self.ipc('clients')
                            if node.get('class') == 'herdcat-sway-test'})
            return all(client.pid in windows for client in clients)

        self.wait('two real xdg-toplevel windows', discover)
        a, b = (windows[client.pid] for client in clients)
        self.check(clients[0].pid != clients[1].pid and a != b,
                   f'fixture PIDs/addresses must differ: {windows}')
        self.dispatch('focuswindow', f'address:0x{b:x}')
        cat_config = self.directory / 'cat.conf'
        cat_config.write_text(
            'keyboard_device=/dev/input/herdcat-hyprland-nonexistent\n'
            'hotplug_scan_interval=3600\nmonitor=WAYLAND-1\nfps=30\n'
            'overlay_opacity=0\nsign_style=fan\nsign_theme=light\n'
            'sign_done=sticky\nagent_done_timeout=30\n'
            'compositor_experimental=1\n')
        self.app = self.spawn('herdcat', [str(BINARY), '-c', str(cat_config)],
                              env=dict(self.env, WAYLAND_DEBUG='client'))
        self.wait('herdcat control socket', lambda:
                  control_ready(self.directory / 'herdcat.sock'))
        self.wait('Hyprland backend ready', lambda:
                  'compositor=Hyprland (experimental) focus-watch=ready'
                  in self.cli('--status'))
        for key, client in zip(KEYS, clients):
            self.wire(f'ev claude working {key} {client.pid}')

        layer_error = None
        try:
            self.wait('layer configure/buffer commit/frame reply', lambda:
                      layer_roundtrip(self.logs['herdcat'].read_text(errors='replace')) and
                      self.overlay_present())
            print('PASS 1: layer configure/ack, committed buffer, frame reply and IPC layer',
                  flush=True)
        except AssertionError as error:
            # Retain the failure, but gather the independent IPC checks in this
            # same launch. Presentation still MUST pass for overall success.
            layer_error = error
            self.check(self.overlay_present(), 'herdcat layer absent from live IPC')
            print(f'FAIL 1: {error}; live IPC layer is present', flush=True)
        print('PASS 2: --status selects Hyprland (experimental), focus-watch=ready', flush=True)
        self.wait('initial tree/PID discovery', lambda:
                  self.mapped(KEYS[0], a, 'no') and self.mapped(KEYS[1], b, 'yes'))
        for key, client in zip(KEYS, clients):
            self.check(f'pid={client.pid} ' in self.session(key),
                       f'session {key} does not retain its real window PID {client.pid}')
        print('PASS 3: --sessions maps both real PIDs to their Hyprland addresses', flush=True)

        # Sticky done on a background window must first be unread. Focus must
        # clear it, while leaving the other background completion unread.
        self.wire(f'ev claude done {KEYS[0]} {clients[0].pid}')
        self.check(' unread' in self.session(KEYS[0]), 'background completion lost unread')
        self.focus(a, KEYS[0], KEYS[1], b)
        self.wait('focus acknowledges unread A', lambda:
                  ' done ' in self.session(KEYS[0]) and
                  ' unread' not in self.session(KEYS[0]))
        self.wire(f'ev claude done {KEYS[1]} {clients[1].pid}')
        self.check(' unread' in self.session(KEYS[1]), 'background B must stay unread')
        print('PASS 4: real focus events mark seen and clear only the focused unread completion', flush=True)

        # --focus reaches focus_session_window(), the same async backend job
        # used by sign release. Assert Hyprland's state, not only command acceptance.
        self.cli('--focus', KEYS[1])
        self.wait('click-path focus reached Hyprland', lambda: self.focused() == b)
        self.wait('click-path focus acknowledged B', lambda:
                  self.mapped(KEYS[1], b, 'yes') and
                  ' done ' in self.session(KEYS[1]) and
                  ' unread' not in self.session(KEYS[1]))
        print('PASS 5: --focus (sign focus job) moves real Hyprland focus to B', flush=True)

        # The fixture honors xdg_toplevel.close but keeps its PID/session alive.
        self.dispatch('closewindow', f'address:0x{b:x}')
        self.wait('closed window removed from Hyprland', lambda:
                  all(int(node['address'], 16) != b for node in self.ipc('clients')))
        self.wait('window close removed from herdcat map', lambda:
                  self.mapped(KEYS[1], 0, 'no'))
        self.check(clients[1].poll() is None and self.session(KEYS[1]),
                   'close must test IPC removal with the process/session still alive')
        self.wait('remaining window still focused', lambda:
                  self.focused() == a and self.mapped(KEYS[0], a, 'yes'))
        clients[1].stdin.write('quit\n')
        clients[1].stdin.flush()
        self.check(clients[1].wait(timeout=3) == 0, 'closed fixture did not exit cleanly')
        self.processes.remove(clients[1])
        clients[1].stdin.close()
        self.wait('pidfd removes closed session', lambda: not self.session(KEYS[1]))
        print('PASS 6: IPC close clears address while PID lives; PID exit removes session', flush=True)

        # Verify the opt-in gate on the same real compositor, then reconnect
        # and rediscover without restarting herdcat or changing default values.
        text = cat_config.read_text()
        cat_config.write_text(text.replace('compositor_experimental=1',
                                           'compositor_experimental=0'))
        self.cli('--reload')
        self.check('compositor=unavailable focus-watch=unavailable' in
                   self.cli('--status'), 'disabled opt-in still selects a backend')
        self.check('window-session' not in self.cli('--sessions'),
                   'disabled opt-in still exposes Hyprland tracking')
        rejected = self.run([str(BINARY), '--focus', KEYS[0]], required=False)
        self.check(rejected.returncode != 0, 'disabled opt-in accepts focus')
        cat_config.write_text(text)
        self.cli('--reload')
        self.wait('opt-in reconnect/initial focused tree', lambda:
                  'compositor=Hyprland (experimental) focus-watch=ready' in
                  self.cli('--status') and self.mapped(KEYS[0], a, 'yes'))
        children_path = Path(f'/proc/{self.app.pid}/task/{self.app.pid}/children')
        children = children_path.read_text().split()
        self.cli('--toggle')
        self.check(self.app.wait(timeout=3) == 0, 'herdcat shutdown failed')
        self.processes.remove(self.app)
        self.check(not (self.directory / 'herdcat.sock').exists(),
                   'herdcat control socket survived shutdown')
        self.check(all(not Path(f'/proc/{pid}').exists() for pid in children),
                   f'herdcat children survived shutdown: {children}')
        print('PASS 7: disabled opt-in, re-enable/reconnect and clean shutdown', flush=True)
        if layer_error:
            # Known when nested in niri 26.04: Hyprland 0.56.2 presents nothing
            # there and so never answers a frame request. The cause could not
            # be told apart from a herdcat fault from inside the test, so this
            # stays a failure; the other six checks are reported above.
            raise layer_error
        print('Hyprland runtime: all 7 checks passed (nested Wayland).', flush=True)

    def diagnostics(self):
        for argv, code, stdout, stderr in self.history:
            print(f'{argv!r}: exit={code}\n{stdout}\n{stderr}', file=sys.stderr)
        for name, path in self.logs.items():
            print(f'--- {name} ---\n' + '\n'.join(
                path.read_text(errors='replace').splitlines()[-100:]), file=sys.stderr)

    def cleanup(self):
        errors = []
        # End the visible window first, including on assertion/interrupt failure.
        self.stop_nested()
        if self.hypr:
            try:
                self.hypr.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.hypr.kill()
                self.hypr.wait(timeout=1)
            self.launch.update(seconds=round(time.monotonic() - self.started, 3),
                               exit=self.hypr.returncode)
        owned_pids = [p.pid for p in self.processes]
        for process in reversed(self.processes):
            try:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=1)
                if process.stdin:
                    process.stdin.close()
            except Exception as error:
                errors.append(str(error))
        if self.hypr:
            self.check(self.hypr.poll() is not None, 'Hyprland survived cleanup')
            for timer in self.watchdogs:
                timer.cancel()
            with self.ledger.open('r+') as ledger:
                fcntl.flock(ledger, fcntl.LOCK_EX)
                launches = json.load(ledger)
                launches[self.launch['number'] - 1] = self.launch
                ledger.seek(0)
                ledger.truncate()
                json.dump(launches, ledger)
            print(f'Nested cleanup: {json.dumps(self.launch)}', flush=True)
        self.check(all(not Path(f'/proc/{pid}').exists() for pid in owned_pids),
                   f'owned processes survived cleanup: {owned_pids}')
        if self.evidence and self.launch:
            self.evidence.mkdir(parents=True, exist_ok=True)
            for name, path in self.logs.items():
                shutil.copyfile(path, self.evidence / f'{name}-{self.launch["number"]}.log')
            shutil.copyfile(self.ledger, self.evidence / 'launches.json')
            (self.evidence / 'commands.json').write_text(json.dumps(list(self.history)))
        if errors:
            raise RuntimeError('; '.join(errors))


def interrupted(signum, frame):
    raise KeyboardInterrupt(f'signal {signum}')


def main():
    if os.environ.get('HERDCAT_HYPRLAND_NESTED') != '1':
        print('SKIP Hyprland runtime (set HERDCAT_HYPRLAND_NESTED=1; '
              'opens a window on your desktop)')
        return 0
    if not Path('/usr/bin/Hyprland').is_file() or not Path('/usr/bin/hyprctl').is_file():
        print('SKIP Hyprland runtime: system Hyprland/hyprctl not found')
        return 0
    host = host_display()
    if host is None:
        print('SKIP Hyprland runtime: host Wayland socket not found')
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    if args.evidence_dir:
        path = args.evidence_dir.resolve()
        if not (path.is_relative_to(ROOT) or path.is_relative_to('/tmp')):
            parser.error('evidence must stay in this worktree or /tmp')
    with tempfile.TemporaryDirectory(prefix='hc-hypr-', dir='/tmp') as tmp:
        directory = Path(tmp)
        directory.chmod(0o700)
        signal.signal(signal.SIGTERM, interrupted)
        test = HyprlandTest(directory, host, args.evidence_dir)
        result = 0
        try:
            test.execute()
        except BaseException as error:
            test.stop_nested()
            print(f'FAIL Hyprland runtime: {error!r}', file=sys.stderr)
            test.diagnostics()
            result = 1
        finally:
            test.cleanup()
        return result


if __name__ == '__main__':
    sys.exit(main())
