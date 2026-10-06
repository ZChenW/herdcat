#!/usr/bin/env python3
"""Validate the /proc sampler against idle, busy and exiting test processes."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

SCRIPT = Path(__file__).with_name("measure_idle.sh")
TARGET = """
import ctypes, mmap, os, signal, sys
ctypes.CDLL(None).prctl(15, b'measure ) odd)', 0, 0, 0)
buffers = []
for name, size in [('herdcat-measure-big-test', 8 << 20),
                   ('herdcat-measure-small-test', 4 << 20)]:
    fd = os.memfd_create(name)
    os.ftruncate(fd, size)
    buffer = mmap.mmap(fd, size)
    for offset in range(0, size, 4096):
        buffer[offset] = 1
    buffers.append(buffer)
print('ready', flush=True)
if sys.argv[1] == 'busy':
    while True:
        pass
signal.pause()
"""


class MeasureIdleTests(unittest.TestCase):
    def invoke(self, *args):
        return subprocess.run([str(SCRIPT), *map(str, args)], capture_output=True,
                              text=True, timeout=5)

    def sample(self, mode):
        with subprocess.Popen([sys.executable, "-c", TARGET, mode],
                              stdout=subprocess.PIPE, text=True) as target:
            try:
                self.assertEqual(target.stdout.readline().strip(), "ready")
                result = self.invoke(target.pid, 0.5)
                self.assertEqual(result.returncode, 0, result.stderr)
                metrics = {}
                for line in result.stdout.splitlines():
                    key, separator, value = line.partition("=")
                    if not separator:
                        break
                    metrics[key] = float(value)
                self.assertEqual(metrics["pid"], target.pid)
                self.assertGreaterEqual(metrics["elapsed_seconds"], 0.5)
                self.assertAlmostEqual(metrics["sample_end_monotonic"] -
                                       metrics["sample_start_monotonic"],
                                       metrics["elapsed_seconds"], places=5)
                expected_cpu = (100 * metrics["cpu_ticks_delta"] /
                                os.sysconf("SC_CLK_TCK") / metrics["elapsed_seconds"])
                self.assertAlmostEqual(metrics["cpu_percent"], expected_cpu, places=2)
                self.assertEqual(metrics['cpu_ticks_delta'],
                                 metrics['user_cpu_ticks_delta'] + metrics['system_cpu_ticks_delta'])
                self.assertAlmostEqual(metrics['cpu_percent'],
                                       metrics['user_cpu_percent'] + metrics['system_cpu_percent'],
                                       places=2)
                rows = result.stdout.split("PERMS ADDRESS PATH\n")[1].splitlines()
                self.assertEqual(len(rows), 8)
                rss = [int(row.split()[0]) for row in rows]
                self.assertEqual(rss, sorted(rss, reverse=True))
                for name, kib in (("big", 8192), ("small", 4096)):
                    row = next(row for row in rows if f"measure-{name}-test" in row)
                    self.assertEqual(int(row.split()[0]), kib)
                self.assertGreaterEqual(metrics["rss_kib"], 12288)
                self.assertGreaterEqual(metrics["pss_kib"], 12288)
                self.assertLessEqual(metrics["pss_kib"], metrics["rss_kib"])
                return metrics
            finally:
                target.terminate()
                target.wait(timeout=3)

    def test_idle_process_and_resident_mappings(self):
        self.assertEqual(self.sample("idle")["cpu_ticks_delta"], 0)

    def test_busy_process_with_parentheses_in_name(self):
        self.assertGreater(self.sample("busy")["cpu_percent"], 5)

    def test_invalid_arguments(self):
        for args in ((), ("0", "1"), ("-1", "1"), ("x", "1"),
                     (os.getpid(), "0"), (os.getpid(), "-1"),
                     (os.getpid(), "nan"), (os.getpid(), "inf"),
                     (os.getpid(), "bad")):
            with self.subTest(args=args):
                result = self.invoke(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("measure_idle:", result.stderr)
                self.assertFalse(result.stdout)

    def test_exit_during_sample(self):
        with subprocess.Popen(["sleep", "0.2"]) as target:
            result = self.invoke(target.pid, 0.5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exited", result.stderr)
            self.assertFalse(result.stdout)


if __name__ == "__main__":
    unittest.main()
