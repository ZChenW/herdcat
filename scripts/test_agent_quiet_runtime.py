#!/usr/bin/env python3
"""Synthetic pty output, hooks and measurements on our private compositor."""
import argparse
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time

from runtime_test_helpers import runtime_env, wait_until
from measure_scenarios import CONFIG, wire, stop

ROOT = Path(__file__).resolve().parent.parent
WORKER = r'''
import ctypes, fcntl, json, os, select, signal, subprocess, sys, termios, time
os.setsid()
fcntl.ioctl(1, termios.TIOCSCTTY, 0)
ctypes.CDLL(None).prctl(15, sys.argv[2].encode(), 0, 0, 0)
active = True
def pause(*args):
    global active
    active = False
def resume(*args):
    global active
    active = True
signal.signal(signal.SIGUSR1, pause)
signal.signal(signal.SIGUSR2, resume)
while True:
    if select.select([sys.stdin], [], [], .05)[0]:
        line = sys.stdin.readline()
        if not line:
            break
        result = subprocess.run([sys.argv[1], '--hook', sys.argv[2]],
                                input=line, text=True, capture_output=True,
                                timeout=3)
        print(json.dumps([result.returncode, result.stdout, result.stderr]),
              file=sys.stderr, flush=True)
    if active:
        os.write(1, b'x' * 750)
'''


def run(binary, agent, measure, baseline=False):
    with tempfile.TemporaryDirectory(prefix='hq-') as directory:
        root = Path(directory)
        transcript = root / 'session.jsonl'
        transcript.write_text('')
        env = runtime_env(HOME=directory, XDG_RUNTIME_DIR=directory,
                          XDG_STATE_HOME=directory, XDG_CONFIG_HOME=directory,
                          XDG_CACHE_HOME=directory, WAYLAND_DISPLAY='wayland-test',
                          XDG_CURRENT_DESKTOP='test')
        config = root / 'cat.conf'
        config.write_text(CONFIG.replace('sign_animations=full',
                                         'sign_animations=off') if measure else CONFIG)
        master, slave = pty.openpty()
        processes = []
        helper = None
        draining = threading.Event()

        def drain():
            while not draining.is_set():
                if select.select([master], [], [], .1)[0]:
                    try:
                        os.read(master, 65536)
                    except OSError:
                        return

        thread = threading.Thread(target=drain)
        thread.start()
        with (root / 'runtime.log').open('w+') as log:
            try:
                server = subprocess.Popen([str(ROOT / 'build/compositor/server')],
                                          env=env, stdout=log, stderr=log)
                processes.append(server)
                wait_until(lambda: (root / 'wayland-test').exists())
                app = subprocess.Popen([binary, '-c', str(config)], env=env,
                                       stdout=log, stderr=log)
                processes.append(app)
                wait_until(lambda: (root / 'herdcat.sock').exists())
                if measure:
                    # Match the existing focus measurement fixture: suspend
                    # only our private helper so device rescans add no wakes.
                    children = Path(f'/proc/{app.pid}/task/{app.pid}/children')
                    wait_until(lambda: children.read_text().strip())
                    ids = children.read_text().split()
                    assert len(ids) == 1, ids
                    helper = int(ids[0])
                    assert b'--input-helper' in Path(f'/proc/{helper}/cmdline').read_bytes()
                    os.kill(helper, signal.SIGSTOP)
                worker = subprocess.Popen([sys.executable, '-u', '-c', WORKER,
                                           binary, agent], env=env,
                                          stdin=subprocess.PIPE, stdout=slave,
                                          stderr=subprocess.PIPE, text=True)
                processes.append(worker)

                def hook(event):
                    worker.stdin.write(json.dumps(dict(hook_event_name=event,
                                                      session_id='quiet-test',
                                                      transcript_path=str(transcript))) + '\n')
                    worker.stdin.flush()
                    assert select.select([worker.stderr], [], [], 4)[0]
                    expected = '{}\n' if agent == 'grok' else ''
                    assert json.loads(worker.stderr.readline()) == [0, expected, '']

                def state(value):
                    rows = wire(root, 'sessions')
                    return value in rows and str(worker.pid) in rows

                hook('UserPromptSubmit')
                assert state('working'), wire(root, 'sessions')
                results = {}
                if measure:
                    # Do not query the control socket during sampling.
                    for label, warmup, seconds in [('recent-working', 1, 7),
                                                   ('older-working', 5, 15)]:
                        time.sleep(warmup)
                        result = subprocess.run([str(ROOT / 'scripts/measure_idle.sh'),
                                                 str(app.pid), str(seconds)],
                                                env=env, text=True,
                                                capture_output=True, timeout=seconds + 5)
                        assert result.returncode == 0, result.stderr
                        results[label] = result.stdout
                        assert state('working')
                else:
                    until = time.monotonic() + 15
                    while time.monotonic() < until:
                        assert state('working'), 'continuous output was interrupted'
                        time.sleep(.15)
                if not measure and not baseline:
                    # Stop between sparse pairs, long after the last hook.
                    time.sleep(2.2)
                    os.kill(worker.pid, signal.SIGUSR1)
                    stopped = time.monotonic()
                    wait_until(lambda: state('idle'), 3,
                               description='long-turn cancellation puts sign away')
                    results['long_cancel_latency_seconds'] = time.monotonic() - stopped
                    assert worker.poll() is None
                    os.kill(worker.pid, signal.SIGUSR2)
                # A fresh submission exercises the fast cancellation path.
                hook('UserPromptSubmit')
                time.sleep(.5)  # Reviewer's just-submitted cancellation timing.
                os.kill(worker.pid, signal.SIGUSR1)
                stopped = time.monotonic()
                if baseline:
                    time.sleep(3)
                    assert state('working')
                    results['cancel_latency_seconds'] = 'still working after 3 seconds'
                    hook('Interrupt')
                else:
                    wait_until(lambda: state('idle'), 3,
                               description='quiet agent puts its sign away')
                    results['cancel_latency_seconds'] = time.monotonic() - stopped
                assert worker.poll() is None
                if measure:
                    time.sleep(11)  # let transitions and surface shrinking finish
                    result = subprocess.run([str(ROOT / 'scripts/measure_idle.sh'),
                                             str(app.pid), '10'], env=env, text=True,
                                            capture_output=True, timeout=15)
                    assert result.returncode == 0, result.stderr
                    results['idle-session'] = result.stdout
                os.kill(worker.pid, signal.SIGUSR2)
                if agent == 'claude' and not baseline:
                    # File events also self-heal, without an idle poll timer.
                    with transcript.open('a') as stream:
                        stream.write(json.dumps(dict(type='assistant', message=dict(
                            role='assistant', content='PRIVATE-QUIET-SENTINEL'))) + '\n')
                    wait_until(lambda: state('working'), 1,
                               description='record activity recovers quiet guess')
                hook('PreToolUse')
                assert state('working'), 'a new hook must recover working'
                hook('PermissionRequest')
                os.kill(worker.pid, signal.SIGUSR1)
                time.sleep(3)
                assert state('waiting')
                hook('Stop')
                time.sleep(3)
                assert state('done')
                hook('SessionEnd')
                assert 'No agent sessions' in wire(root, 'sessions')
                print(json.dumps(dict(agent=agent, results=results)), flush=True)
                wire(root, 'stop')
                assert app.wait(timeout=3) == 0
                log.flush()
                output = (root / 'runtime.log').read_text()
                assert 'PRIVATE-QUIET-SENTINEL' not in output
                assert 'AddressSanitizer' not in output and 'runtime error:' not in output
            finally:
                if helper is not None and Path(f'/proc/{helper}').exists():
                    os.kill(helper, signal.SIGCONT)
                for process in reversed(processes):
                    stop(process)
                draining.set()
                thread.join(timeout=2)
                os.close(master)
                os.close(slave)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', default=str(ROOT / 'build/herdcat'))
    parser.add_argument('--agent', choices=['claude', 'grok'], default='claude')
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    run(args.binary, args.agent, args.measure, args.baseline)
