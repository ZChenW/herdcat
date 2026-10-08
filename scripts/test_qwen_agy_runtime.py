#!/usr/bin/env python3
"""Synthetic Qwen/Antigravity turns; private HOME, compositor and log FD."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from runtime_test_helpers import HookParent, WORKER, runtime_env, run_on_pty, wait_until

run_on_pty()
binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())

with tempfile.TemporaryDirectory(prefix='hc-qa-') as directory:
    root = Path(directory)
    env = runtime_env(HOME=directory, XDG_RUNTIME_DIR=directory,
                      XDG_STATE_HOME=directory, XDG_CONFIG_HOME=directory,
                      WAYLAND_DISPLAY='wayland-test', XDG_CURRENT_DESKTOP='test')
    config = root / 'cat.conf'
    config.write_text('keyboard_device=/dev/input/herdcat-test-nonexistent\n'
                      'monitor=TEST-1\nfps=1\nhotplug_scan_interval=0\n'
                      'agent_stale_timeout=0\n')
    agy_log = root / '.gemini/antigravity-cli/log/cli-test.log'
    agy_log.parent.mkdir(parents=True)
    agy_log.write_text('historical log\n')
    # A newer instance's "latest" link must never select its log.
    other = agy_log.with_name('cli-other.log')
    other.write_text('')
    (agy_log.parent.parent / 'cli.log').symlink_to(other)
    log = (root / 'runtime.log').open('w+')
    server = subprocess.Popen([fixture], env=env, stdout=log, stderr=log)
    app = qwen = agy = None

    def wait(condition):
        return wait_until(condition, 3, description='Qwen/agy runtime',
                          diagnostics=lambda: (root / 'runtime.log').read_text()[-4000:])

    def wire(request):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
            client.settimeout(2)
            client.connect(str(root / 'herdcat.sock'))
            client.sendall(request.encode())
            reply = client.recv(4096).decode()
        assert reply.startswith('0 '), reply
        return reply

    def state(expected):
        return f'agent={expected}' in wire('status')

    def qevent(event, **extra):
        qwen.invoke('qwen', dict(hook_event_name=event, session_id='qwen-test',
                                cwd='/tmp/project', **extra))

    def aevent(event, **extra):
        agy.invoke('agy', dict(conversationId='test-id',
                              workspacePaths=['/tmp/project', '/tmp/other'],
                              transcriptPath=str(root / 'unused.jsonl'), **extra), event)

    def cancel(path=agy_log, conversation='test-id', newline=True):
        with path.open('a') as stream:
            stream.write('I1008 09:08:36.312548 2668 conversation_manager.go:1520] '
                         'Cancelling in-progress response for conversation ' + conversation
                         + ('\n' if newline else ''))

    try:
        wait(lambda: (root / 'wayland-test').exists())
        app = subprocess.Popen([binary, '-c', str(config)], env=env, stdout=log, stderr=log)
        wait(lambda: (root / 'herdcat.sock').exists())
        qwen = HookParent(binary, env)
        qevent('SessionStart')
        assert state('idle')
        qevent('UserPromptSubmit', prompt='first question')
        assert state('working')
        qevent('PermissionRequest')
        assert state('waiting')
        qevent('Notification', notification_type='permission_prompt')
        assert state('waiting')
        for event in ('PreToolUse', 'PostToolUse', 'PostToolBatch', 'UserPromptSubmit'):
            qevent(event, prompt='')
            assert state('working')
        assert 'first question' in wire('sessions')
        qevent('Stop')
        qevent('Notification', notification_type='idle_prompt')
        assert state('done')
        for _ in range(2):  # Early and mid-response Esc have identical hooks.
            qevent('UserPromptSubmit')
            qevent('Notification', notification_type='idle_prompt')
            assert state('idle')
        qevent('UserPromptSubmit')
        qevent('Notification', notification_type='idle_prompt')
        time.sleep(.015)
        qevent('StopFailure')
        assert state('error')
        qevent('SessionEnd')
        assert 'No agent sessions' in wire('sessions')
        qwen.close()
        qwen = None

        agy = HookParent.__new__(HookParent)
        worker = 'log_fd = open(__import__("sys").argv[2], "a")\n' + WORKER
        agy.process = subprocess.Popen([sys.executable, '-u', '-c', worker, binary, str(agy_log)],
                                       env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
        aevent('PreInvocation', invocationNum=0)
        assert state('working')
        assert f'pid={agy.process.pid}' in wire('sessions')
        for event in ('PreToolUse', 'PostToolUse', 'PostInvocation', 'PreInvocation', 'PostInvocation'):
            aevent(event, error='')
            assert state('working')  # No permission callback means no waiting.
        aevent('Stop', error='', fullyIdle=True, terminationReason='NO_TOOL_CALL')
        assert state('done')
        cancel()
        assert state('done')
        for _ in range(2):
            aevent('PreInvocation')
            cancel(other)  # Latest link belongs to another instance.
            cancel(conversation='other-id')
            cancel(conversation='test-id-extra')
            time.sleep(.1)
            assert state('working')
            cancel(newline=False)
            time.sleep(.1)
            assert state('working')
            with agy_log.open('a') as stream:
                stream.write('\n')
            wait(lambda: state('idle'))
            assert f'pid={agy.process.pid}' in wire('sessions')
        aevent('PreInvocation')
        aevent('Stop', error='PRIVATE-ERROR')
        assert state('error')
        agy.close()
        agy = None
        wait(lambda: 'No agent sessions' in wire('sessions'))
        wire('stop')
        assert app.wait(timeout=3) == 0
        output = (root / 'runtime.log').read_text()
        assert 'PRIVATE-ERROR' not in output and 'Cancelling in-progress' not in output
        assert 'AddressSanitizer' not in output and 'runtime error:' not in output
        print('Qwen/agy runtime: turns, empty prompts, errors, cancellation log and PID exit passed.')
    finally:
        for parent in (qwen, agy):
            if parent:
                parent.close()
        for process in (app, server):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
        log.close()
