#!/usr/bin/env python3
"""Client-only status tests against a private authenticated socket fixture."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / 'build/herdcat'
REPLY = b'0 visible=1 paused=0\nfocus=none\ninput-helper=test'
STATUS = REPLY[2:] + b'\n'
HINT = b'Agent integrations need setup: tmux; run herdcat setup.\n'


class StatusHintTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='hc-status-', dir='/tmp')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.env = dict(PATH=str(self.bin), HOME=str(self.root),
                        XDG_RUNTIME_DIR=str(self.root),
                        XDG_CONFIG_HOME=str(self.root / 'config'),
                        XDG_STATE_HOME=str(self.root / 'state'),
                        XDG_CACHE_HOME=str(self.root / 'cache'),
                        PYTHONDONTWRITEBYTECODE='1', LC_MESSAGES='en_US.UTF-8')
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.server.bind(str(self.root / 'herdcat.sock'))
        self.server.listen()
        self.server.settimeout(3)
        self.addCleanup(self.server.close)

    def helper(self, body):
        script = self.bin / 'herdcat-setup'
        script.write_text('#!' + sys.executable + '\nimport os,sys,time\n' + body)
        script.chmod(0o700)

    def client(self, command='--status'):
        received = []
        def respond():
            with self.server.accept()[0] as connection:
                received.append(connection.recv(512))
                connection.sendall(REPLY)
        thread = threading.Thread(target=respond)
        thread.start()
        started = time.monotonic()
        try:
            result = subprocess.run([str(BINARY), command], env=self.env,
                                    capture_output=True, timeout=3)
        finally:
            thread.join(timeout=3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(received, [command[2:].encode()])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b'')
        return result.stdout, time.monotonic() - started

    def test_appends_only_hint_and_preserves_every_existing_byte(self):
        self.helper('assert sys.argv[1:] == ["--status-hint"]\n'
                    f'sys.stdout.buffer.write({HINT!r})\n')
        self.assertEqual(self.client()[0], STATUS + HINT)
        self.assertEqual(self.client('--sessions')[0], STATUS)

    def test_current_missing_script_and_missing_python_are_silent(self):
        self.assertEqual(self.client()[0], STATUS)
        self.helper('pass\n')
        self.assertEqual(self.client()[0], STATUS)
        (self.bin / 'herdcat-setup').write_text('#!/usr/bin/env python3\n')
        self.assertEqual(self.client()[0], STATUS)

    def test_failure_partial_output_and_oversized_output_are_discarded(self):
        for body in (f'print({HINT.decode()!r}); sys.exit(1)\n',
                     'print("x" * 8192)\n',
                     'print("unexpected\\nmultiple lines")\n',
                     'sys.stdout.write("partial"); sys.stdout.flush(); time.sleep(30)\n'):
            with self.subTest(body=body):
                self.helper(body)
                self.assertEqual(self.client()[0], STATUS)

    def test_deadline_kills_and_reaps_its_own_child(self):
        pidfile = self.root / 'child'
        self.helper(f'open({str(pidfile)!r}, "w").write(str(os.getpid()))\n'
                    'sys.stdout.close(); time.sleep(30)\n')
        output, elapsed = self.client()
        self.assertEqual(output, STATUS)
        self.assertGreaterEqual(elapsed, 1.9)
        self.assertLess(elapsed, 2.3)
        self.assertFalse(Path('/proc/' + pidfile.read_text()).exists())

    def test_real_setup_uses_private_config_without_modifying_it(self):
        (self.bin / 'python3').symlink_to(sys.executable)
        (self.bin / 'herdcat-setup').symlink_to(ROOT / 'scripts/herdcat-setup')
        config = self.root / 'config/tmux/tmux.conf'
        config.parent.mkdir(parents=True)
        old = (ROOT / 'tests/setup_fixtures/tmux/old.conf').read_bytes()
        config.write_bytes(old)
        self.assertEqual(self.client()[0], STATUS + HINT)
        self.assertEqual(config.read_bytes(), old)
        self.assertFalse((self.root / 'state').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
