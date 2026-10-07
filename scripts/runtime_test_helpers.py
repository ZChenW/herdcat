"""Process fixtures for runtime tests; never imported by the product."""
import errno
import json
import os
import pty
import select
import signal
import subprocess
import sys
import time


def run_on_pty():
    """Continue in a child with its own controlling terminal, relay its exit."""
    sys.stdout.flush()
    sys.stderr.flush()
    pid, master = pty.fork()
    if pid == 0:
        return
    try:
        while True:
            try:
                data = os.read(master, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        _, status = os.waitpid(pid, 0)
    except BaseException:
        os.killpg(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
        raise
    finally:
        os.close(master)
    code = os.waitstatus_to_exitcode(status)
    sys.exit(code if code >= 0 else 128 - code)


WORKER = """
import json, os, subprocess, sys
for line in sys.stdin:
    request = json.loads(line)
    payload = request['payload']
    if request['agent'] == 'pi':
        payload['agent_pid'] = os.getpid()
    args = [sys.argv[1], '--hook', request['agent']]
    if request['event']:
        args.extend(['--event', request['event']])
    result = subprocess.run(args, input=json.dumps(payload), text=True,
                            capture_output=True, timeout=3)
    print(json.dumps(dict(code=result.returncode, stdout=result.stdout,
                          stderr=result.stderr)), flush=True)
"""


class HookParent:
    """Keep one real parent PID alive for the lifetime of a synthetic session."""
    def __init__(self, binary, env):
        self.process = subprocess.Popen([sys.executable, '-u', '-c', WORKER, binary],
                                        env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True)

    def invoke(self, agent, payload, event=None, json_stdout=False):
        process = self.process
        process.stdin.write(json.dumps(dict(agent=agent, payload=payload, event=event)) + '\n')
        process.stdin.flush()
        assert select.select([process.stdout], [], [], 4)[0], 'hook parent timed out'
        line = process.stdout.readline()
        assert line, 'hook parent exited without a result'
        result = json.loads(line)
        assert result == dict(code=0, stdout='{}\n' if json_stdout else '', stderr=''), result

    def close(self):
        process = self.process
        try:
            process.stdin.close()
            process.wait(timeout=3)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3)
            process.stdout.close()
            process.stderr.close()


def runtime_env(**overrides):
    """Ignore host terminal/compositor metadata and confine all state to fixtures."""
    env = dict(os.environ)
    for key in ('NIRI_SOCKET', 'HYPRLAND_INSTANCE_SIGNATURE', 'SWAYSOCK',
                'KITTY_PID', 'KITTY_WINDOW_ID', 'KITTY_LISTEN_ON', 'TMUX',
                'TMUX_PANE', 'WEZTERM_PANE', 'WEZTERM_UNIX_SOCKET',
                'TERM_PROGRAM', 'WAYLAND_DEBUG', 'HERDCAT_HOOK_DEBUG',
                'HERDCAT_TEST_DRAG', 'HERDCAT_TEST_MEASURE',
                'HERDCAT_TEST_RELEASE_MS', 'CLAUDE_PID'):
        env.pop(key, None)
    env.update(overrides)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    return env


def wait_until(sample, seconds=6, *, description='fixture condition',
               diagnostics=None):
    """Poll a bounded condition and report its last value plus fixture logs."""
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        last = sample()
        if last:
            return last
        time.sleep(.03)
    detail = diagnostics() if diagnostics else None
    raise AssertionError(f'{description} timed out after {seconds}s; '
                         f'last={last!r};diagnostics={detail!r}')


def wait_settled(sample, seconds=8, *, ready=None, quiet=.3, minimum=.65,
                 description='fixture geometry', diagnostics=None):
    """Sample only after transitions and a continuous quiet geometry interval.

    The minimum covers the longest model transition (500ms), even when an
    old committed frame remains unchanged before the next callback arrives.
    ready can require a fresh commit or the expected lifecycle phase.
    """
    started = stable_since = time.monotonic()
    deadline = started + seconds
    previous = last = None
    while time.monotonic() < deadline:
        last = sample()
        now = time.monotonic()
        if last != previous:
            previous, stable_since = last, now
        if last is None or (ready is not None and not ready(last)):
            stable_since = now
        elif now - started >= minimum and now - stable_since >= quiet:
            return last
        time.sleep(.03)
    detail = diagnostics() if diagnostics else None
    raise AssertionError(f'{description} did not settle after {seconds}s; '
                         f'last={last!r};previous={previous!r};diagnostics={detail!r}')
