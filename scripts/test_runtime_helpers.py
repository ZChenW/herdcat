#!/usr/bin/env python3
"""Readiness policy tests with a simulated clock; no sockets or compositor."""
import os
import unittest
from unittest.mock import patch

import runtime_test_helpers as helpers


class Clock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


class WaitTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.time = patch.object(helpers, 'time', self.clock)
        self.time.start()
        self.addCleanup(self.time.stop)

    def test_delayed_commit_and_transition(self):
        # An old frame is initially quiet; the callback arrives at 400ms and
        # the last transition frame at 600ms. Sampling must wait past both.
        def sample():
            if self.clock.now < .4:
                return (10, 10)
            if self.clock.now < .6:
                return (20, 20)
            return (30, 30)
        self.assertEqual(helpers.wait_settled(sample), (30, 30))
        self.assertGreaterEqual(self.clock.now, .9)

    def test_ready_requires_new_phase(self):
        def sample():
            return {'phase': int(self.clock.now >= 1)}
        result = helpers.wait_settled(sample, ready=lambda value: value['phase'] == 1)
        self.assertEqual(result, {'phase': 1})
        self.assertGreaterEqual(self.clock.now, 1.3)

    def test_failure_has_snapshot_and_log(self):
        with self.assertRaisesRegex(AssertionError, 'server exited') as error:
            helpers.wait_settled(lambda: None, seconds=.2,
                                 diagnostics=lambda: 'server exited')
        self.assertIn('last=None', str(error.exception))
        with self.assertRaisesRegex(AssertionError, 'fixture output'):
            helpers.wait_until(lambda: False, seconds=.2,
                               diagnostics=lambda: 'fixture output')

    def test_host_terminal_metadata_is_removed(self):
        with patch.dict(os.environ, {'TMUX': '/host,1,0', 'NIRI_SOCKET': '/host',
                                     'KITTY_PID': '42', 'HERDCAT_TEST_DRAG': '1'}):
            env = helpers.runtime_env(NIRI_SOCKET='/fixture')
        self.assertNotIn('TMUX', env)
        self.assertNotIn('KITTY_PID', env)
        self.assertNotIn('HERDCAT_TEST_DRAG', env)
        self.assertEqual(env['NIRI_SOCKET'], '/fixture')


if __name__ == '__main__':
    unittest.main()
