#!/usr/bin/env bash
# Replay against a dedicated running overlay. The supplied PID is stopped at end.
# Usage: BONGOCAT_BIN=./build/bongocat scripts/replay_agent_hooks.sh \
#          --config /path/to/test.conf --pid <test-overlay-pid>
# Optional real niri check: --focus-pid <owned-kitty-child> --focus-window <id>
set -euo pipefail
exec python3 - "$@" <<'PY'
import argparse
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description='Replay hooks against an empty test overlay; stops it on success.')
parser.add_argument('--config', type=Path, required=True)
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--focus-pid', type=int, help='PID inside an owned kitty window')
parser.add_argument('--focus-window', type=int, help='Expected niri window ID')
args = parser.parse_args()
if (args.focus_pid is None) != (args.focus_window is None):
    parser.error('--focus-pid and --focus-window must be supplied together')
binary = os.environ.get('BONGOCAT_BIN', 'bongocat')
env = dict(os.environ)
env.pop('BONGOCAT_HOOK_DEBUG', None)
original = args.config.read_bytes()
mode = args.config.stat().st_mode & 0o777
child = None
stopped = False

def control(name, success=True, *arguments):
    result = subprocess.run([binary, '--' + name, *arguments], env=env, capture_output=True,
                            text=True, timeout=3)
    assert (result.returncode == 0) == success, (name, result.stdout, result.stderr)
    return result.stdout.strip()

def status(state, sessions):
    text = control('status')
    assert f'pid={args.pid} ' in text, text
    assert f'agent={state} sessions={sessions}' in text, text
    return text

def wait_state(state, sessions, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        text = control('status')
        if f'agent={state} sessions={sessions}' in text:
            return
        time.sleep(.05)
    raise AssertionError((state, sessions, text))

def hook(event, session, **fields):
    payload = {'hook_event_name': event, 'session_id': session, **fields}
    result = subprocess.run([binary, '--hook', 'claude'], env=env,
                            input=json.dumps(payload), text=True,
                            capture_output=True, timeout=3)
    assert result.returncode == 0 and not result.stdout and not result.stderr, result

def config(stale, extra=""):
    text = original.decode().rstrip() + f'\n[global]\nsign_done=timeout\nagent_done_timeout=5\nagent_stale_timeout={stale}\n' + extra
    replace(text.encode())
    control('reload')

def replace(data):
    fd, temp = tempfile.mkstemp(prefix=args.config.name + '.', dir=args.config.parent)
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, 'wb') as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, args.config)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)

def wire(request, success=True):
    runtime = env.get('XDG_RUNTIME_DIR')
    path = str(Path(runtime) / 'bongocat.sock') if runtime else f'/tmp/bongocat-{os.getuid()}.sock'
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
        client.settimeout(2)
        client.connect(path)
        client.sendall(request.encode())
        reply = client.recv(512).decode()
    assert reply.startswith('0 ') == success, (request, reply)
    return reply

def niri(*arguments):
    result = subprocess.run(['niri', 'msg', *arguments], env=env,
                            capture_output=True, text=True, timeout=3, check=True)
    return result.stdout

names = ['demo-a', '工具', 'same', 'same', 'long-name-abcdefghijklmnop', 'demo-f']
focus_key = '1111222233334444'

initial = control('status')
assert f'pid={args.pid} ' in initial and f'config={args.config} ' in initial, initial
assert 'sessions=0' in initial, 'Use an empty, dedicated overlay; existing sessions are preserved.'
try:
    config(600)
    for i, name in enumerate(names):
        hook('SessionStart', f'replay-name-{i}', cwd='/synthetic/' + name)
    lines = control('sessions').splitlines()
    assert len(lines) == 6 and [line.split(maxsplit=5)[5] for line in lines] == names, lines
    hook('UserPromptSubmit', 'replay-name-5', cwd='/synthetic/' + names[5])
    lines = control('sessions').splitlines()
    assert [line.split(maxsplit=5)[5] for line in lines] == names, lines
    for style in ('fan', 'post'):
        config(600, f'sign_style={style}\nsign_language=zh\n')
        hook('PermissionRequest', 'replay-name-1', cwd='/synthetic/' + names[1])
        time.sleep(.7)  # Render transitions and populate text/bitmap caches.
        status('waiting', 6)
        hook('PostToolUse', 'replay-name-1')
    control('focus', False, 'eeeeeeeeeeeeeeee')
    for i in range(6):
        hook('SessionEnd', f'replay-name-{i}')
    status('idle', 0)
    if args.focus_pid is not None:
        previous = json.loads(niri('-j', 'focused-window'))
        assert previous and previous['id'] != args.focus_window, 'Start from another window.'
        wire(f'ev claude start {focus_key} {args.focus_pid}')
        wire(f'name {focus_key} demo-focus')
        try:
            for key in (focus_key, focus_key[:8]):
                niri('action', 'focus-window', '--id', str(previous['id']))
                control('focus', True, key)
                deadline = time.monotonic() + 3
                while True:
                    focused = json.loads(niri('-j', 'focused-window'))
                    if focused and focused['id'] == args.focus_window:
                        break
                    assert time.monotonic() < deadline, 'focus request did not reach the kitty window'
                    time.sleep(.03)
        finally:
            wire(f'ev claude end {focus_key} 0')
            niri('action', 'focus-window', '--id', str(previous['id']))
        print('PASS: real niri kitty focus with full key and unique prefix.')
    else:
        print('NOT RUN: real kitty focus (supply --focus-pid and --focus-window).')
    print('PASS: six session names/order, both styles, nonexistent focus key.')
    config(600)
    hook('UserPromptSubmit', 'replay-A')
    status('working', 1)
    hook('UserPromptSubmit', 'replay-B')
    hook('PermissionRequest', 'replay-A')
    status('waiting', 2)
    for event in ('PreToolUse', 'PostToolUse'):
        hook(event, 'replay-B')
        status('waiting', 2)
    hook('PostToolUse', 'replay-A')
    status('working', 2)
    hook('Stop', 'replay-A')
    status('done', 2)
    wait_state('working', 2, 6)
    hook('Notification', 'replay-B', notification_type='idle_prompt')
    status('idle', 2)
    hook('SessionEnd', 'replay-A')
    hook('SessionEnd', 'replay-B')
    status('idle', 0)
    # Establish the hook parent, then exec sleep with the same PID.
    code = '''import os, subprocess, sys
payload = b'{"hook_event_name":"PermissionRequest","session_id":"replay-process"}'
subprocess.run([sys.argv[1], '--hook', 'claude'], input=payload, check=True)
os.execv('/usr/bin/sleep', ['sleep', '30'])
'''
    child = subprocess.Popen([sys.executable, '-c', code, binary], env=env)
    wait_state('waiting', 1, 3)
    deadline = time.monotonic() + 3
    while Path(f'/proc/{child.pid}/comm').read_text().strip() != 'sleep':
        assert time.monotonic() < deadline
        time.sleep(.02)
    # A pidfd tag must have zero upper bits: nonzero bits identify extra fds.
    infos = [entry.read_text() for entry in Path(f'/proc/{args.pid}/fdinfo').iterdir()]
    tags = [int(match.group(1), 16) for info in infos
            for match in re.finditer(r'tfd:.*? data:\s*([0-9a-fA-F]+)', info)]
    assert child.pid in tags, ('pidfd tag corrupted', child.pid, tags)
    child.terminate()
    child.wait(timeout=3)
    wait_state('idle', 0, 2)
    config(3)
    hook('UserPromptSubmit', 'replay-stale')
    status('working', 1)
    wait_state('idle', 1, 4)
    hook('SessionEnd', 'replay-stale')
    status('idle', 0)
    config(600)

    def agent_hook(agent, payload, event=None, json_stdout=False):
        command = [binary, '--hook', agent]
        if event:
            command.extend(['--event', event])
        result = subprocess.run(command, env=env, input=json.dumps(payload),
                                text=True, capture_output=True, timeout=3)
        expected = '{}\n' if json_stdout else ''
        assert result.returncode == 0 and result.stdout == expected and not result.stderr, (
            agent, event, result.returncode, result.stdout, result.stderr)

    def one_session(agent, state, name):
        lines = control('sessions').splitlines()
        assert len(lines) == 1, lines
        parts = lines[0].split(maxsplit=5)
        assert parts[0] == agent and parts[2] == state and parts[5] == name, lines
        status(state, 1)

    def finish(agent, payload, event=None, json_stdout=False):
        agent_hook(agent, payload, event, json_stdout)
        assert control('sessions') == 'No agent sessions'
        status('idle', 0)

    # beforeShellExecution stays mapped in the adapter, but it is a Cursor
    # approval gate and is intentionally not part of this installed sequence.
    sequences = [
        ('claude', False, None, '/x/replay-claude', 'replay-claude', [
            ({'hook_event_name': 'UserPromptSubmit'}, 'working'),
            ({'hook_event_name': 'PermissionRequest'}, 'waiting'),
            ({'hook_event_name': 'Stop'}, 'done'),
        ], {'hook_event_name': 'SessionEnd'}),
        ('codex', False, None, '/x/replay-codex', 'replay-codex', [
            ({'hook_event_name': 'UserPromptSubmit'}, 'working'),
            ({'hook_event_name': 'Interrupt'}, 'idle'),
        ], {'hook_event_name': 'SessionEnd'}),
        ('grok', True, None, '/x/replay-grok', 'replay-grok', [
            ({'hook_event_name': 'UserPromptSubmit'}, 'working'),
            ({'hook_event_name': 'StopCancelled'}, 'idle'),
            ({'hook_event_name': 'PermissionRequest'}, 'waiting'),
            ({'hook_event_name': 'Stop'}, 'done'),
        ], {'hook_event_name': 'SessionEnd'}),
        ('kimi', False, None, '/x/replay-kimi', 'replay-kimi', [
            ({'hook_event_name': 'UserPromptSubmit'}, 'working'),
            ({'hook_event_name': 'PermissionRequest'}, 'waiting'),
            ({'hook_event_name': 'PermissionResult'}, 'working'),
            ({'hook_event_name': 'Interrupt'}, 'idle'),
        ], {'hook_event_name': 'SessionEnd'}),
        ('cursor', True, True, '/x/replay-cursor', 'replay-cursor', [
            ({}, 'working', 'beforeSubmitPrompt'),
            ({}, 'working', 'preToolUse'),
            ({'status': 'completed'}, 'done', 'stop'),
        ], ({}, 'sessionEnd')),
        ('copilot', True, True, '/x/replay-copilot', 'replay-copilot', [
            ({}, 'working', 'userPromptSubmitted'),
            ({}, 'working', 'permissionRequest'),
            ({'notification_type': 'permission_prompt'}, 'waiting', 'notification'),
            ({'stopReason': 'end_turn'}, 'done', 'agentStop'),
        ], ({}, 'sessionEnd')),
        ('pi', False, True, '/x/replay-pi', 'replay-pi', [
            ({'agent_pid': os.getpid()}, 'working', 'before_agent_start'),
            ({'agent_pid': os.getpid(), 'stopReason': 'stop'}, 'done', 'agent_end'),
        ], ({'agent_pid': os.getpid()}, 'session_shutdown')),
        ('opencode', False, True, '/x/replay-opencode', 'replay-opencode', [
            ({}, 'working', 'session.execution.started'),
            ({}, 'waiting', 'permission.asked'),
            ({}, 'working', 'permission.replied'),
            ({}, 'done', 'session.execution.succeeded'),
        ], ({}, 'session.deleted')),
    ]
    for agent, json_stdout, explicit, cwd, name, steps, end in sequences:
        identity = {'session_id': name, 'cwd': cwd}
        if agent == 'cursor':
            identity = {'conversation_id': name, 'workspace_roots': [cwd]}
        elif agent == 'copilot':
            identity = {'sessionId': name, 'cwd': cwd}
        elif agent == 'pi':
            identity = {'session_id': name, 'cwd': cwd}
        for step in steps:
            event = step[2] if explicit else None
            payload = dict(identity)
            payload.update(step[0])
            agent_hook(agent, payload, event, json_stdout)
            one_session(agent, step[1], name)
        end_event = end[1] if explicit else None
        end_payload = dict(identity)
        end_payload.update(end[0] if explicit else end)
        finish(agent, end_payload, end_event, json_stdout)
    print('PASS: claude, codex, grok, kimi, cursor, copilot, pi, opencode sessions.')
    replace(original)
    control('reload')
    # Send stop directly, so a disappearing instance cannot toggle a new one on.
    runtime = env.get('XDG_RUNTIME_DIR')
    path = str(Path(runtime) / 'bongocat.sock') if runtime else f'/tmp/bongocat-{os.getuid()}.sock'
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control_socket:
        control_socket.settimeout(2)
        control_socket.connect(path)
        control_socket.sendall(b'stop')
        assert control_socket.recv(512).startswith(b'0 ')

    deadline = time.monotonic() + 3
    while subprocess.run([binary, '--status'], env=env, capture_output=True).returncode == 0:
        assert time.monotonic() < deadline
        time.sleep(.05)
    stopped = True
    hook('UserPromptSubmit', 'replay-absent')
    print('PASS: priorities, per-session done, rest/end, pidfd exit, stale timeout and quiet absent overlay.')
finally:
    if child and child.poll() is None:
        child.terminate()
        child.wait(timeout=3)
    replace(original)
    if not stopped:
        try:
            wire(f'ev claude end {focus_key} 0')
        except OSError:
            pass
        for session in ('replay-A', 'replay-B', 'replay-stale', 'replay-process',
                        *(f'replay-name-{i}' for i in range(6))):
            hook('SessionEnd', session)
        subprocess.run([binary, '--reload'], env=env, capture_output=True)
PY
