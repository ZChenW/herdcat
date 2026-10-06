"""Process fixtures for runtime tests; never imported by the product."""
import errno
import json
import os
import pty
import select
import signal
import subprocess
import sys


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
