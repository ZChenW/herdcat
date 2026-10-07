#!/usr/bin/env python3
"""Exercise the real unprivileged libc helper using pipes, never sockets."""
import os
from pathlib import Path
import select
import struct
import subprocess
import unittest

HELPER = Path(__file__).resolve().parents[1] / "build/herdcat-input"


class InputHelperTests(unittest.TestCase):
    def test_environment_and_parent_close(self):
        for interval, paths in ((0, []), (1, []),
                                (0, ["/dev/input/event2147483647"])):
            with self.subTest(interval=interval, paths=paths):
                reader, writer = os.pipe()
                env = dict(os.environ, HERDCAT_CONFIG="/forbidden/config",
                           HERDCAT_DEBUG="1", LD_PRELOAD="/missing/library.so")
                # ld.so processes LD_PRELOAD before main in an unprivileged
                # executable. Use benign loader values; test helper-owned env.
                env.pop("LD_PRELOAD")
                process = subprocess.Popen(
                    [str(HELPER), str(writer), str(interval), "1", str(len(paths)), *paths],
                    pass_fds=(writer,), env=env,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(writer)
                try:
                    self.assertTrue(select.select([reader], [], [], 3)[0])
                    packet = os.read(reader, 24)
                    self.assertEqual(len(packet), 24)
                    paws, devices, denied, reserved, stamp = struct.unpack(
                        "=IIIIq", packet)
                    self.assertEqual((paws, devices, denied, reserved), (0, 0, 0, 0))
                    self.assertGreater(stamp, 0)
                    status = Path(f"/proc/{process.pid}/status").read_text()
                    self.assertIn("NoNewPrivs:\t1", status)
                    self.assertIn("Seccomp:\t2", status)
                    self.assertEqual(list(map(int, next(
                        row for row in status.splitlines()
                        if row.startswith("Gid:")).split()[1:])), [os.getgid()] * 4)
                    self.assertIsNone(process.poll())
                    os.close(reader)
                    reader = -1
                    stdout, stderr = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, 0)
                    self.assertEqual(stdout, b"")
                    self.assertEqual(stderr, b"")
                finally:
                    if reader >= 0:
                        os.close(reader)
                    if process.poll() is None:
                        process.kill()
                        process.communicate()

    def test_rejected_arguments_leave_no_output(self):
        cases = [[], ["1", "0", "1", "0"], ["3", "-1", "1", "0"],
                 ["3", "3601", "1", "0"], ["3", "0", "2", "0"],
                 ["3", "0", "1", "33"],
                 ["3", "0", "1", "1", "/dev/input/event1/../event2"]]
        for args in cases:
            with self.subTest(args=args):
                result = subprocess.run([str(HELPER), *args], capture_output=True,
                                        timeout=3)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    unittest.main()
