#!/usr/bin/env python3
"""Real, isolated headless Sway acceptance; run outside restricted sandboxes.

Only a missing sway executable may SKIP. HERDCAT_REQUIRE_SWAY=1 makes that
an error in CI. No recorded IPC payloads, desktop sockets or input devices
are used. All failures dump live IPC queries, command results and log tails.
"""
from collections import deque
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from runtime_test_helpers import runtime_env

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / 'build/herdcat'
CLIENT = ROOT / 'build/sway_toplevel_fixture'
KEYS = ('aaaaaaaaaaaaaaaa', 'bbbbbbbbbbbbbbbb')


def nodes(tree):
    yield tree
    for key in ('nodes', 'floating_nodes'):
        for child in tree.get(key, []):
            yield from nodes(child)


def layer_roundtrip(text):
    """Correlate this overlay's configure, buffer commit and frame reply.

    Sway does not expose layer surfaces in GET_TREE/GET_OUTPUTS. A received
    frame callback for the committed overlay is stronger than a client-only
    trace of an outgoing wl_surface.commit. Accept libwayland's @/# notation.
    """
    for created in re.finditer(
            r'get_layer_surface\(new id zwlr_layer_surface_v1[@#](\d+), '
            r'wl_surface[@#](\d+), [^\n]*"herdcat-overlay"', text):
        layer, surface = created.groups()
        trace = text[created.end():]
        configured = re.search(
            rf'zwlr_layer_surface_v1[@#]{layer}\.configure\((\d+),', trace)
        if not configured:
            continue
        acknowledged = re.search(
            rf'zwlr_layer_surface_v1[@#]{layer}\.ack_configure'
            rf'\({configured.group(1)}\)', trace[configured.end():])
        if not acknowledged:
            continue
        trace = trace[configured.end() + acknowledged.end():]
        attached = re.search(
            rf'wl_surface[@#]{surface}\.attach\(wl_buffer[@#]\d+,', trace)
        if not attached:
            continue
        trace = trace[attached.end():]
        frame = re.search(
            rf'wl_surface[@#]{surface}\.frame\(new id wl_callback[@#](\d+)\)',
            trace)
        if not frame:
            continue
        trace = trace[frame.end():]
        commit = re.search(rf'wl_surface[@#]{surface}\.commit\(\)', trace)
        if commit and re.search(
                rf'wl_callback[@#]{frame.group(1)}\.done\(',
                trace[commit.end():]):
            return True
    return False


class SwayTest:
    def __init__(self, directory):
        self.directory = directory
        self.env = runtime_env(
            XDG_RUNTIME_DIR=str(directory), XDG_STATE_HOME=str(directory / 'state'),
            XDG_CONFIG_HOME=str(directory / 'config'),
            XDG_CACHE_HOME=str(directory / 'cache'), HOME=str(directory),
            WLR_BACKENDS='headless', WLR_LIBINPUT_NO_DEVICES='1',
            WLR_RENDERER='pixman', WLR_HEADLESS_OUTPUTS='1')
        for key in ('WAYLAND_DISPLAY', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS',
                    'SWAYSOCK', 'NIRI_SOCKET', 'HYPRLAND_INSTANCE_SIGNATURE'):
            self.env.pop(key, None)
        self.processes = []
        self.logs = {}
        self.history = deque(maxlen=16)
        self.sway = self.app = None

    @staticmethod
    def check(condition, message):
        if not condition:
            raise AssertionError(message)

    def spawn(self, name, argv, env=None, stdin=None):
        path = self.directory / f'{name}.log'
        self.logs[name] = path
        with path.open('w') as log:
            process = subprocess.Popen(argv, cwd=ROOT, env=env or self.env,
                                       stdin=stdin, stdout=log, stderr=log,
                                       text=True, start_new_session=True)
        self.processes.append(process)
        return process

    def run(self, argv, required=True, timeout=3):
        try:
            result = subprocess.run(argv, cwd=ROOT, env=self.env, text=True,
                                    capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            self.history.append((argv, 'timeout', error.stdout, error.stderr))
            raise
        self.history.append((argv, result.returncode, result.stdout, result.stderr))
        if required:
            self.check(result.returncode == 0,
                       f'{argv!r} exited {result.returncode}: '
                       f'{result.stdout}\n{result.stderr}')
        return result

    def ipc(self, *args):
        return json.loads(self.run(['swaymsg', '-s', self.env['SWAYSOCK'],
                                   '-r', *args]).stdout)

    def sway_command(self, command):
        reply = self.ipc(command)
        self.check(isinstance(reply, list) and reply and
                   all(row.get('success') is True for row in reply),
                   f'Sway command failed: {command}: {reply!r}')

    def cli(self, *args):
        return self.run([str(BINARY), *args]).stdout

    def wire(self, request):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
            control.settimeout(2)
            control.connect(str(self.directory / 'herdcat.sock'))
            control.sendall(request.encode())
            reply = control.recv(1280).decode()
        self.history.append((['control', request], 0, reply, ''))
        self.check(reply.startswith('0 '), f'{request}: {reply}')
        return reply

    def wait(self, description, sample, seconds=6):
        deadline = time.monotonic() + seconds
        last = None
        while time.monotonic() < deadline:
            for process in self.processes:
                self.check(process.poll() is None,
                           f'owned process {process.args} exited {process.returncode}')
            last = sample()
            if last:
                return last
            time.sleep(.05)
        raise AssertionError(f'{description} timed out after {seconds}s; last={last!r}')

    def tree(self):
        return self.ipc('-t', 'get_tree')

    def focused(self):
        return next((node['id'] for node in nodes(self.tree())
                     if node.get('pid') and node.get('focused')), 0)

    def session(self, key):
        return next((row for row in self.cli('--sessions').splitlines()
                     if row.startswith('claude ' + key[:8] + ' ')), '')

    def mapped(self, key, window, seen):
        return (f'sway-session {key[:8]} con_id={window} seen={seen}'
                in self.cli('--sessions').splitlines())

    def focus(self, window, key, other_key, other_window):
        self.sway_command(f'[con_id={window}] focus')
        self.wait('Sway focus command', lambda: self.focused() == window)
        self.wait('herdcat focus event', lambda:
                  self.mapped(key, window, 'yes') and
                  self.mapped(other_key, other_window, 'no'))

    def execute(self):
        self.check(shutil.which('swaymsg') is not None, 'swaymsg is required')
        version = self.run(['sway', '--version']).stdout.strip()
        print(f'Runtime: {version}; headless + pixman', flush=True)
        self.run(['make', 'all', 'sway-runtime-build'], timeout=180)
        config = self.directory / 'sway.conf'
        config.write_text('xwayland disable\noutput HEADLESS-1 mode 1280x720\n'
                          'seat seat0 fallback true\nfocus_follows_mouse no\n')
        self.sway = self.spawn('sway', ['sway', '-d', '-c', str(config)])
        ipc_path = self.directory / f'sway-ipc.{os.getuid()}.{self.sway.pid}.sock'
        self.env['SWAYSOCK'] = str(ipc_path)
        self.wait('private IPC socket', ipc_path.exists)
        self.wait('private Wayland socket', lambda:
                  list(self.directory.glob('wayland-*')) and
                  any(path.is_socket() for path in self.directory.glob('wayland-*')))
        sockets = [path for path in self.directory.glob('wayland-*') if path.is_socket()]
        self.check(len(sockets) == 1, f'expected one private Wayland socket: {sockets}')
        self.env['WAYLAND_DISPLAY'] = sockets[0].name
        def output_ready():
            outputs = self.ipc('-t', 'get_outputs')
            return (len(outputs) == 1 and outputs[0]['name'] == 'HEADLESS-1' and
                    outputs[0]['active'])

        self.wait('single active HEADLESS-1 output', output_ready)

        clients = [self.spawn(f'client-{i}', [str(CLIENT), f'Sway fixture {i}'],
                              stdin=subprocess.PIPE) for i in range(2)]
        windows = {}

        def discover():
            windows.clear()
            windows.update({node['pid']: node['id'] for node in nodes(self.tree())
                            if node.get('app_id') == 'herdcat-sway-test'})
            return all(client.pid in windows for client in clients)

        self.wait('two real xdg-toplevel windows', discover)
        a, b = (windows[client.pid] for client in clients)
        self.check(clients[0].pid != clients[1].pid and a != b,
                   f'fixture PIDs/con_ids must differ: {windows}')
        self.sway_command(f'[con_id={b}] focus')
        cat_config = self.directory / 'cat.conf'
        cat_config.write_text(
            'keyboard_device=/dev/input/herdcat-sway-nonexistent\n'
            'hotplug_scan_interval=3600\nmonitor=HEADLESS-1\nfps=30\n'
            'overlay_opacity=0\nsign_style=fan\nsign_theme=light\n'
            'sign_done=sticky\nagent_done_timeout=30\n'
            'compositor_experimental=1\n')
        self.app = self.spawn('herdcat', [str(BINARY), '-c', str(cat_config)],
                              env=dict(self.env, WAYLAND_DEBUG='client'))
        self.wait('herdcat control socket', lambda:
                  (self.directory / 'herdcat.sock').exists())
        self.wait('Sway backend ready', lambda:
                  'compositor=Sway (experimental) focus-watch=ready'
                  in self.cli('--status'))
        for key, client in zip(KEYS, clients):
            self.wire(f'ev claude working {key} {client.pid}')

        self.wait('layer configure/buffer commit/frame reply', lambda:
                  layer_roundtrip(self.logs['herdcat'].read_text(errors='replace')) and
                  'new layer surface: namespace herdcat-overlay'
                  in self.logs['sway'].read_text(errors='replace'))
        print('PASS 1: layer created, configured, buffer committed and frame replied', flush=True)
        print('PASS 2: --status selects Sway (experimental), focus-watch=ready', flush=True)
        self.wait('initial tree/PID discovery', lambda:
                  self.mapped(KEYS[0], a, 'no') and self.mapped(KEYS[1], b, 'yes'))
        for key, client in zip(KEYS, clients):
            self.check(f'pid={client.pid} ' in self.session(key),
                       f'session {key} does not retain its real window PID {client.pid}')
        print('PASS 3: --sessions maps both real PIDs to their Sway con_ids', flush=True)

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
        # used by sign release. Assert Sway's state, not only command acceptance.
        self.cli('--focus', KEYS[1])
        self.wait('click-path focus reached Sway', lambda: self.focused() == b)
        self.wait('click-path focus acknowledged B', lambda:
                  self.mapped(KEYS[1], b, 'yes') and
                  ' done ' in self.session(KEYS[1]) and
                  ' unread' not in self.session(KEYS[1]))
        print('PASS 5: --focus (sign focus job) moves real Sway focus to B', flush=True)

        # The fixture honors xdg_toplevel.close but keeps its PID/session alive.
        self.sway_command(f'[con_id={b}] kill')
        self.wait('closed window removed from Sway', lambda:
                  all(node['id'] != b for node in nodes(self.tree())))
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
        print('PASS 6: IPC close clears con_id while PID lives; PID exit removes session', flush=True)

        # Verify the opt-in gate on the same real compositor, then reconnect
        # and rediscover without restarting herdcat or changing default values.
        text = cat_config.read_text()
        cat_config.write_text(text.replace('compositor_experimental=1',
                                           'compositor_experimental=0'))
        self.cli('--reload')
        self.check('compositor=unavailable focus-watch=unavailable' in
                   self.cli('--status'), 'disabled opt-in still selects a backend')
        self.check('sway-session' not in self.cli('--sessions'),
                   'disabled opt-in still exposes Sway tracking')
        rejected = self.run([str(BINARY), '--focus', KEYS[0]], required=False)
        self.check(rejected.returncode != 0, 'disabled opt-in accepts focus')
        cat_config.write_text(text)
        self.cli('--reload')
        self.wait('opt-in reconnect/initial focused tree', lambda:
                  'compositor=Sway (experimental) focus-watch=ready' in
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
        print(f'Sway runtime: all 7 checks passed ({version}; headless/pixman).', flush=True)

    def diagnostics(self):
        print('\n--- Sway runtime failure diagnostics ---', file=sys.stderr, flush=True)
        for argv, code, stdout, stderr in self.history:
            print(f'$ {argv!r}\nexit={code}\n{stdout}\n{stderr}', file=sys.stderr)
        if 'SWAYSOCK' in self.env:
            for kind in ('get_version', 'get_outputs', 'get_tree'):
                try:
                    self.run(['swaymsg', '-s', self.env['SWAYSOCK'], '-r', '-t', kind],
                             required=False)
                    argv, code, stdout, stderr = self.history[-1]
                    print(f'$ {argv!r}\nexit={code}\n{stdout}\n{stderr}', file=sys.stderr)
                except Exception as error:
                    print(f'swaymsg {kind}: {error!r}', file=sys.stderr)
        else:
            print('swaymsg output: private Sway socket not created yet', file=sys.stderr)
        for option in ('--status', '--sessions'):
            try:
                result = self.run([str(BINARY), option], required=False)
                print(f'herdcat {option}: exit={result.returncode}\n'
                      f'{result.stdout}\n{result.stderr}', file=sys.stderr)
            except Exception as error:
                print(f'herdcat {option}: {error!r}', file=sys.stderr)
        names = dict.fromkeys(('herdcat', 'sway', *self.logs))
        for name in names:
            path = self.logs.get(name)
            if path is None:
                print(f'--- {name} log tail: process not started ---', file=sys.stderr)
                continue
            lines = path.read_text(errors='replace').splitlines()
            print(f'--- {name} log tail (200 lines) ---\n' + '\n'.join(lines[-200:]),
                  file=sys.stderr)
        print('--- end diagnostics ---', file=sys.stderr, flush=True)

    def cleanup(self):
        # Only signal/reap processes owned by this test. Sway is stopped last.
        errors = []
        for process in reversed(self.processes):
            try:
                if process.poll() is None:
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait(timeout=3)
                if process.stdin:
                    process.stdin.close()
            except Exception as error:
                errors.append(f'{process.args!r}: {error!r}')
        if errors:
            raise RuntimeError('; '.join(errors))


def main():
    if shutil.which('sway') is None:
        if os.environ.get('HERDCAT_REQUIRE_SWAY') == '1':
            print('FAIL: sway is required (HERDCAT_REQUIRE_SWAY=1)', file=sys.stderr)
            return 1
        print('SKIP Sway runtime: sway executable not found')
        return 0
    # Keep AF_UNIX paths well below 108 bytes, independent of the repo path.
    with tempfile.TemporaryDirectory(prefix='hc-sway-', dir='/tmp') as temporary:
        directory = Path(temporary)
        directory.chmod(0o700)
        test = SwayTest(directory)
        result = 0
        try:
            test.execute()
        except Exception as error:
            print(f'FAIL Sway runtime: {error!r}', file=sys.stderr)
            test.diagnostics()
            result = 1
        finally:
            try:
                test.cleanup()
            except Exception as error:
                print(f'FAIL Sway runtime cleanup: {error!r}', file=sys.stderr)
                test.diagnostics()
                result = 1
    return result


if __name__ == '__main__':
    sys.exit(main())
