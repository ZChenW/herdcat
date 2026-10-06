#!/usr/bin/env python3
"""Exercise real bounded jobs with PATH-injected fixed terminal executables."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(os.environ.get('HERDCAT_TERMINAL_TEST_BINARY',
                             str(ROOT / 'build/test_terminal_focus')))
FIXTURES = ROOT / 'tests/terminal_fixtures'


def run(terminal, mode='ok', operation='focus', nested=False):
    with tempfile.TemporaryDirectory(prefix='herdcat-terminal-jobs-') as tmp:
        socket = Path(tmp) / 'socket with spaces'
        socket.touch()
        log = Path(tmp) / 'commands'
        env = dict(os.environ, PATH=str(FIXTURES), TERMINAL_TEST_LOG=str(log),
                   TERMINAL_TEST_MODE=mode, PYTHONDONTWRITEBYTECODE='1',
                   WEZTERM_UNIX_SOCKET='/invalid/inherited-socket')
        for key in ('KITTY_WINDOW_ID', 'KITTY_LISTEN_ON', 'KITTY_PID', 'TMUX', 'TMUX_PANE'):
            env.pop(key, None)
        if nested:
            env.update(KITTY_WINDOW_ID='7', KITTY_LISTEN_ON='unix:/tmp/kitty-fixture')
        started = time.monotonic()
        result = subprocess.run([str(BINARY), terminal, str(socket), operation],
                                env=env, capture_output=True, text=True, timeout=4)
        elapsed = time.monotonic() - started
        assert result.returncode == 0, (terminal, mode, result)
        commands = [json.loads(line) for line in log.read_text().splitlines()]
        for command in commands:
            assert not Path('/proc', str(command['pid'])).exists(), command
        return result.stdout.strip(), commands, str(socket), elapsed


for mode in ('ok', 'kitty-error'):
    result, commands, _, _ = run('kitty', mode)
    assert result == '1'
    assert [(c['tool'], c['args']) for c in commands] == [
        ('niri', ['msg', '-j', 'windows']),
        ('niri', ['msg', 'action', 'focus-window', '--id', '111']),
        ('kitten', ['@', '--to', 'unix:/tmp/kitty-fixture', 'focus-window', '--match', 'id:7'])]
result, commands, socket, _ = run('tmux')
assert result == '1', result
assert [(c['tool'], c['args']) for c in commands] == [
    ('tmux', ['-S', socket, 'display-message', '-p', '-t', '%0', '#{session_name}']),
    ('tmux', ['-S', socket, 'list-clients', '-F',
              '#{client_pid} #{client_session} #{client_tty} #{client_activity}']),
    ('niri', ['msg', '-j', 'windows']),
    ('niri', ['msg', 'action', 'focus-window', '--id', '111']),
    ('tmux', ['-S', socket, 'switch-client', '-c', '/dev/pts/4', '-t', '%0'])]
result, commands, _, _ = run('tmux', nested=True)
assert result == '1'
assert [c['tool'] for c in commands] == ['tmux', 'tmux', 'niri', 'niri', 'kitten', 'tmux']
assert commands[-2]['args'] == ['@', '--to', 'unix:/tmp/kitty-fixture', 'focus-window', '--match', 'id:7']
result, commands, _, _ = run('tmux', 'detached')
assert result == '2' and len(commands) == 2
result, commands, _, _ = run('tmux', 'detached', operation='background')
assert result.split() == ['0', '1'] and len(commands) == 2
# Both client hooks use the same bounded server-notification request. Exercise
# both replies, all matching rows, and isolation from another server.
for mode in ('ok', 'detached'):
    result, commands, socket, _ = run('tmux', mode, operation='notification')
    assert result == '0 2' and len(commands) == 4, (result, commands)
    assert all(c['tool'] == 'tmux' and c['args'][:2] == ['-S', socket]
               for c in commands)
    assert [c['args'][2] for c in commands] == [
        'display-message', 'list-clients', 'display-message', 'list-clients']
result, commands, socket, _ = run('wezterm')
assert result == '1'
assert [(c['tool'], c['args']) for c in commands] == [
    ('wezterm', ['cli', 'activate-pane', '--pane-id', '0']),
    ('wezterm', ['cli', 'list', '--format', 'json']),
    ('niri', ['msg', '-j', 'windows']),
    ('niri', ['msg', 'action', 'focus-window', '--id', '222'])]
assert all(c['socket'] == socket for c in commands if c['tool'] == 'wezterm')
result, commands, _, _ = run('wezterm', operation='deferred')
assert result == '1'
assert [c['tool'] for c in commands] == ['wezterm', 'wezterm', 'niri', 'niri', 'wezterm']
assert commands[-1]['args'] == ['cli', 'list-clients', '--format', 'json']
result, commands, _, _ = run('ghostty')
assert result == '1' and len(commands) == 2
assert commands[-1]['args'][-1] == '222'
result, commands, _, _ = run('wezterm', operation='current')
assert result.split()[:2] == ['1', '0'] and len(commands) == 1
assert commands[0]['args'] == ['cli', 'list-clients', '--format', 'json']
result, commands, _, _ = run('wezterm', operation='current-again')
assert result.split()[:2] == ['1', '0'] and len(commands) == 2
result, commands, _, _ = run('wezterm', 'ambiguous', 'current')
assert result.split()[0] == '0' and len(commands) == 1
for terminal in ('tmux', 'wezterm'):
    for mode in ('error', 'overflow', 'malformed', 'timeout'):
        result, commands, _, elapsed = run(terminal, mode)
        expected = '3'
        assert result == expected, (terminal, mode, result)
        assert elapsed < 1.7 and (mode != 'timeout' or elapsed >= .9), elapsed
    result, commands, _, _ = run(terminal, operation='background')
    assert result.split()[0] == '0'
print('Terminal argv, nesting, detached clients, title selection, current-pane dedupe, failure bounds and reaping passed.')
