#!/usr/bin/env bash
# Replay against a dedicated running overlay. The supplied PID is stopped at end.
# Usage: BONGOCAT_BIN=./build/bongocat scripts/replay_agent_hooks.sh \
#          --config /path/to/test.conf --pid <test-overlay-pid>
set -euo pipefail
exec python3 - "$@" <<'PY'
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description='Replay hooks against an empty test overlay; stops it on success.')
parser.add_argument('--config', type=Path, required=True)
parser.add_argument('--pid', type=int, required=True)
args = parser.parse_args()
binary = os.environ.get('BONGOCAT_BIN', 'bongocat')
env = dict(os.environ)
env.pop('BONGOCAT_HOOK_DEBUG', None)
original = args.config.read_bytes()
mode = args.config.stat().st_mode & 0o777
child = None
stopped = False

def control(name, success=True):
    result = subprocess.run([binary, '--' + name], env=env, capture_output=True,
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

def config(stale):
    text = original.decode().rstrip() + f'\n[global]\nagent_done_timeout=5\nagent_stale_timeout={stale}\n'
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

initial = control('status')
assert f'pid={args.pid} ' in initial and f'config={args.config} ' in initial, initial
assert 'sessions=0' in initial, 'Use an empty, dedicated overlay; existing sessions are preserved.'
try:
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
    child.terminate()
    child.wait(timeout=3)
    wait_state('idle', 0, 2)
    config(3)
    hook('UserPromptSubmit', 'replay-stale')
    status('working', 1)
    wait_state('idle', 1, 4)
    hook('SessionEnd', 'replay-stale')
    status('idle', 0)
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
        for session in ('replay-A', 'replay-B', 'replay-stale', 'replay-process'):
            hook('SessionEnd', session)
        subprocess.run([binary, '--reload'], env=env, capture_output=True)
PY
