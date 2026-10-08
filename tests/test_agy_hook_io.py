#!/usr/bin/env python3
"""Antigravity hooks cannot emit policy bytes, even on malformed/stalled input."""
import os
from pathlib import Path
import pty
import subprocess
import tempfile
import time
import unittest

BINARY = str(Path('build/herdcat').resolve())


class AntigravityIOTests(unittest.TestCase):
    def test_quiet_inputs(self):
        with tempfile.TemporaryDirectory(prefix='hc-agy-io-') as home:
            env = dict(os.environ, HOME=home, XDG_RUNTIME_DIR=home,
                       XDG_CONFIG_HOME=home, XDG_STATE_HOME=home)
            for key in ('WAYLAND_DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'CLAUDE_PID',
                        'HERDCAT_HOOK_DEBUG', 'HERDCAT_HYPRLAND_NESTED'):
                env.pop(key, None)
            for event in ('PreInvocation', 'PreToolUse', 'PostToolUse', 'PostInvocation',
                          'Stop', 'Unknown'):
                for payload in (b'', b'{', b'{}', b'[]', b'null', b'{} trailing',
                                b'{"conversationId":"test","error":""}',
                                b'{"error":"private","decision":"allow"}',
                                b'{"error":"' + b'x' * 100000 + b'"}'):
                    result = subprocess.run([BINARY, '--hook', 'agy', '--event', event],
                                            env=env, input=payload, capture_output=True,
                                            timeout=3)
                    self.assertEqual((result.returncode, result.stdout, result.stderr),
                                     (0, b'', b''))
            process = subprocess.Popen([BINARY, '--hook', 'agy', '--event', 'PreToolUse'],
                                       env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE)
            started = time.monotonic()
            try:
                self.assertEqual(process.wait(timeout=3), 0)
                self.assertTrue(1.5 <= time.monotonic() - started < 3)
                self.assertEqual(process.stdout.read(), b'')
                self.assertEqual(process.stderr.read(), b'')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
                process.stdin.close()
                process.stdout.close()
                process.stderr.close()
            master, slave = pty.openpty()
            try:
                result = subprocess.run([BINARY, '--hook', 'agy', '--event', 'PreToolUse'],
                                        env=env, stdin=slave, capture_output=True, timeout=1)
                self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b'', b''))
            finally:
                os.close(master)
                os.close(slave)


if __name__ == '__main__':
    unittest.main()
