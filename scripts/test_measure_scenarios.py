#!/usr/bin/env python3
"""Check compositor counts against the sampler's actual time window."""
import sys
import json
from pathlib import Path
import socket
import tempfile
import unittest

sys.dont_write_bytecode = True
from measure_scenarios import fixture_sample
from measure_focus_fixture import FocusFixture


class FixtureSampleTests(unittest.TestCase):
    def test_owned_niri_and_pty_ancestry_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='herdcat-focus-measure-test-') as tmp:
            fixture = FocusFixture(Path(tmp), 2).start()
            pids = [fixture.agent_pid]
            try:
                with socket.socket(socket.AF_UNIX) as connection:
                    connection.settimeout(3)
                    connection.connect(str(fixture.path))
                    connection.sendall(b'"EventStream"\n')
                    event = json.loads(connection.recv(4096))
                self.assertEqual(event['WindowsChanged']['windows'][0]['pid'], fixture.pid)
                for _ in range(2):
                    fields = Path(f'/proc/{pids[-1]}/stat').read_text().rsplit(')', 1)[1].split()
                    self.assertGreater(int(fields[4]), 0)  # controlling PTY
                    pids.append(int(fields[1]))
                self.assertEqual(pids[-1], fixture.pid)
            finally:
                fixture.close()
            self.assertFalse(any(Path(f'/proc/{pid}').exists() for pid in pids))

    def test_boundaries_output_scope_and_intervals(self):
        metrics = dict(sample_start_monotonic=10, sample_end_monotonic=40)
        log = """ready
submit TEST-1 9.9
submit TEST-2 10.0
submit TEST-1 10.0
frame-request TEST-1 10.0
submit TEST-1 10.18
frame-done TEST-1 10.02
release TEST-1 10.01
release TEST-2 10.02
release TEST-1 40.0
submit TEST-1 10.36
commit TEST-1 2560x262
submit TEST-1 40.0
frame-request TEST-1 40.0
"""
        result = fixture_sample(log, metrics)
        self.assertEqual(result['buffer_commits_in_sample'], 3)
        self.assertAlmostEqual(result['buffer_commits_per_second'], 0.1)
        self.assertEqual(result['frame_requests_in_sample'], 1)
        self.assertEqual(result['frame_callbacks_in_sample'], 1)
        self.assertEqual(result['buffer_releases_in_sample'], 1)
        self.assertAlmostEqual(result['buffer_releases_per_second'], 1 / 30)
        self.assertEqual(result['fixture_events_ms']['release'], [10])
        self.assertEqual(result['fixture_events_ms']['submit'], [0, 180, 360])
        for value in result['submission_intervals_ms'].values():
            self.assertAlmostEqual(value, 180)

    def test_repaint_accumulates_damage_until_each_buffer_is_used(self):
        metrics = dict(sample_start_monotonic=10, sample_end_monotonic=12)
        log = ('paint-buffer TEST-1 9 7 100 80\n'
               'paint-buffer TEST-1 9.2 8 100 80\n'
               'damage TEST-1 10.5 3 4 20 30\n'
               'paint-buffer TEST-1 10.5 7 100 80\n'
               'damage TEST-1 11.5 0 0 10 20\n'
               'paint-buffer TEST-1 11.5 8 100 80\n')
        result = fixture_sample(log, metrics)
        self.assertEqual(result['repaint_pixels_per_commit'],
                         dict(min=600, median=691, max=782))

    def test_damage_uses_sample_window_and_output(self):
        metrics = dict(sample_start_monotonic=10, sample_end_monotonic=12)
        log = ('damage TEST-1 9 0 0 999 999\n'
               'damage TEST-2 10.5 0 0 999 999\n'
               'damage TEST-1 10.5 3 4 20 30\n'
               'damage TEST-1 11.5 0 0 10 20\n'
               'damage TEST-1 12 0 0 999 999\n')
        result = fixture_sample(log, metrics)
        self.assertEqual(result['damage_pixels_per_commit'],
                         dict(min=200, median=400, max=600))
        self.assertIsNone(fixture_sample('', metrics)['damage_pixels_per_commit'])

    def test_quiet_window_excludes_setup_and_teardown(self):
        metrics = dict(sample_start_monotonic=100, sample_end_monotonic=130)
        result = fixture_sample('submit TEST-1 99\nframe-done TEST-1 99.5\n'
                                'submit TEST-1 131\n', metrics)
        for key in ('buffer_commits_in_sample', 'buffer_commits_per_second',
                    'frame_requests_in_sample', 'frame_callbacks_in_sample',
                    'buffer_releases_in_sample', 'buffer_releases_per_second'):
            self.assertEqual(result[key], 0)
        self.assertIsNone(result['submission_intervals_ms'])


if __name__ == '__main__':
    unittest.main()
