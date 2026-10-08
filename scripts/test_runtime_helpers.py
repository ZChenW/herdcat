#!/usr/bin/env python3
"""Readiness policy tests with a simulated clock; no sockets or compositor."""
import os
from pathlib import Path
import json
import tempfile
import subprocess
import unittest
from unittest.mock import Mock, patch

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

    def test_fixture_timing_is_explicit_and_real_cases_cannot_inherit_it(self):
        with patch.dict(os.environ, {'HERDCAT_TEST_TIMING': 'fast',
                                     'WAYLAND_DISPLAY': 'host',
                                     'WAYLAND_SOCKET': '7',
                                     'HERDCAT_HYPRLAND_NESTED': '1'}):
            env = helpers.runtime_env()
            for key in ('HERDCAT_TEST_TIMING', 'WAYLAND_DISPLAY',
                        'WAYLAND_SOCKET', 'HERDCAT_HYPRLAND_NESTED'):
                self.assertTrue(key not in env, key)
            self.assertEqual(helpers.runtime_timing(real_time=True), (1, {}))
            self.assertEqual(helpers.runtime_timing(),
                             (.25, {'HERDCAT_TEST_TIMING': 'fast'}))

    def test_tier_matrix_retains_one_complete_real_duration_case(self):
        import test_surface_tiers_runtime as tiers
        with patch.object(tiers, 'run') as run:
            tiers.main()
        cases = run.call_args_list
        self.assertEqual(len(cases), 13)
        real = [call for call in cases if call.kwargs.get('real_time')]
        self.assertEqual(len(real), 1)
        self.assertEqual(real[0].args, ('fan', False, 120))
        with patch.object(tiers, 'run') as run:
            tiers.main(real_time=True)
        self.assertTrue(all(call.kwargs['real_time'] for call in run.call_args_list))

    def test_fast_quiet_cases_overlap_but_real_windows_remain_exclusive(self):
        result = subprocess.run(['make', '-n', 'test-runtime'],
                                cwd=Path(__file__).resolve().parent.parent,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        for agent in ('claude', 'grok', 'copilot'):
            command = f"python3 scripts/test_agent_quiet_runtime.py --agent {agent}"
            self.assertEqual(result.stdout.count(command), 1)
            self.assertIn(f"--test '{command}'", result.stdout)
        self.assertIn("--exclusive 'python3 scripts/test_agent_quiet_runtime.py --real-time'",
                      result.stdout)
        self.assertIn("--exclusive 'python3 scripts/test_surface_tiers_runtime.py'",
                      result.stdout)


class HyprlandSafetyTests(unittest.TestCase):
    def test_default_does_not_probe_or_launch_desktop(self):
        import test_hyprland_runtime as hypr
        with patch.dict(os.environ, {}, clear=True), \
                patch.object(hypr, 'host_display') as display, \
                patch.object(hypr.HyprlandTest, 'execute') as execute:
            self.assertEqual(hypr.main(), 0)
        display.assert_not_called()
        execute.assert_not_called()

    def test_env_whitelist(self):
        import test_hyprland_runtime as hypr
        with patch.dict(os.environ, {'NIRI_SOCKET': '/host',
                                     'WAYLAND_DISPLAY': 'host-display',
                                     'WAYLAND_SOCKET': '7', 'SWAYSOCK': '/host',
                                     'DBUS_SESSION_BUS_ADDRESS': '/host'}):
            env = hypr.isolated_env(Path('/tmp/fixture'))
        self.assertEqual(env['HOME'], '/tmp/fixture')
        self.assertEqual(env['XDG_RUNTIME_DIR'], '/tmp/fixture')
        for key in ('NIRI_SOCKET', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET',
                    'SWAYSOCK', 'HYPRLAND_INSTANCE_SIGNATURE',
                    'DBUS_SESSION_BUS_ADDRESS'):
            self.assertNotIn(key, env)

    def test_budget_refuses_sixteenth_launch_before_spawn(self):
        import test_hyprland_runtime as hypr
        with tempfile.TemporaryDirectory(prefix='hc-budget-', dir='/tmp') as tmp:
            directory = Path(tmp)
            test = hypr.HyprlandTest(directory, Path('/host'), None)
            test.ledger = directory / 'ledger.json'
            test.ledger.write_text(json.dumps([{}] * 15))
            test.spawn = Mock()
            with patch.dict(os.environ, {'HERDCAT_HYPRLAND_BUDGET': '15'}):
                with self.assertRaisesRegex(AssertionError, 'budget exhausted'):
                    test.start_nested()
            test.spawn.assert_not_called()

    def test_watchdogs_and_host_display_scope(self):
        import test_hyprland_runtime as hypr
        with tempfile.TemporaryDirectory(prefix='hc-budget-', dir='/tmp') as tmp:
            directory = Path(tmp)
            test = hypr.HyprlandTest(directory, Path('/host/display'), None)
            test.ledger = directory / 'ledger.json'
            process = Mock(pid=123)
            process.poll.return_value = None
            test.spawn = Mock(return_value=process)
            with patch.object(hypr.threading, 'Timer') as timer, \
                    patch.object(hypr.resource, 'setrlimit'):
                test.start_nested()
            self.assertEqual([call.args[0] for call in timer.call_args_list], [35, 38])
            env = test.spawn.call_args.kwargs['env']
            self.assertEqual(env['WAYLAND_DISPLAY'], '/host/display')
            self.assertNotIn('WAYLAND_DISPLAY', test.env)
            self.assertEqual(env['SEATD_SOCK'], str(directory/'no-seat.sock'))
            self.assertEqual(env['AQ_DRM_DEVICES'], str(directory/'no-gpu'))
            self.assertEqual(len(json.loads(test.ledger.read_text())), 1)
            test.stop_nested()
            process.send_signal.assert_called_once_with(hypr.signal.SIGTERM)
            process.poll.return_value = 0
            test.stop_nested(hypr.signal.SIGKILL)
            process.send_signal.assert_called_once()


if __name__ == '__main__':
    unittest.main()
