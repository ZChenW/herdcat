#!/usr/bin/env python3
"""Exercise test scheduling through the runner's command-line interface."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).with_name('run_tests.py')


class RunnerTests(unittest.TestCase):
  def setUp(self):
    self.directory = tempfile.TemporaryDirectory(prefix='hc-runner-', dir='/tmp')
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)

  def command(self, name, body):
    path = self.root / (name + '.py')
    path.write_text(body)
    return shlex.join([sys.executable, str(path)])

  def run_tests(self, commands, jobs=2, exclusive=(), env=None):
    args = [sys.executable, str(RUNNER), '--jobs', str(jobs)]
    for command in commands:
      args.extend(['--test', command])
    for command in exclusive:
      args.extend(['--exclusive', command])
    return subprocess.run(args, capture_output=True, text=True,
               timeout=10, env=env)

  def test_tests_overlap_and_output_is_printed_in_whole_blocks(self):
    commands = []
    for name, peer in [('alpha', 'beta'), ('beta', 'alpha')]:
      commands.append(self.command(name, f'''
from pathlib import Path
import time
root = Path({str(self.root)!r})
(root / {name!r}).touch()
deadline = time.monotonic() + 2
while not (root / {peer!r}).exists():
  assert time.monotonic() < deadline, 'tests did not overlap'
  time.sleep(.01)
for i in range(5):
  print({name!r} + str(i), flush=True)
  time.sleep(.01)
'''))
    result = self.run_tests(commands)
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    for name in ('alpha', 'beta'):
      self.assertIn('\n'.join(name + str(i) for i in range(5)), result.stdout)
    self.assertIn('All tests passed.', result.stdout)

  def test_each_test_has_private_state_and_descriptor_limit(self):
    command = self.command('environment', """
import json, os, resource
from pathlib import Path
keys = ('HOME', 'XDG_RUNTIME_DIR', 'XDG_STATE_HOME', 'XDG_CONFIG_HOME',
    'XDG_CACHE_HOME', 'XDG_DATA_HOME', 'TMPDIR')
root = Path(os.environ['XDG_RUNTIME_DIR'])
assert root.is_dir() and root.stat().st_mode & 0o777 == 0o700
assert str(root).startswith('/tmp/') and len(str(root)) < 40
assert not (root / 'collision').exists()
(root / 'collision').touch()
assert resource.getrlimit(resource.RLIMIT_NOFILE)[0] == 1024
assert resource.getrlimit(resource.RLIMIT_CORE)[0] == 0
for key in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'NIRI_SOCKET', 'SWAYSOCK',
      'HERDCAT_HYPRLAND_NESTED', 'DBUS_SESSION_BUS_ADDRESS', 'CLAUDE_PID',
      'CODEX_HOME', 'KIMI_CODE_HOME', 'GROK_HOME', 'COPILOT_HOME',
      'CLAUDE_CONFIG_DIR', 'PI_CODING_AGENT_DIR', 'OPENCODE_CONFIG',
      'OPENCODE_CONFIG_DIR', 'OPENCODE_CONFIG_CONTENT', 'HERDCAT_TEST_TIMING'):
  assert key not in os.environ, key
assert all(os.environ[key].startswith(str(root)) for key in keys)
print(json.dumps(str(root)))
""")
    env = dict(os.environ, WAYLAND_DISPLAY='host', WAYLAND_SOCKET='7',
         NIRI_SOCKET='/host', SWAYSOCK='/host', CLAUDE_PID='42',
         DBUS_SESSION_BUS_ADDRESS='/host',
         CODEX_HOME='/host', KIMI_CODE_HOME='/host',
         GROK_HOME='/host', COPILOT_HOME='/host', TMPDIR='/host')
    env.update(CLAUDE_CONFIG_DIR='/host', PI_CODING_AGENT_DIR='/host',
               OPENCODE_CONFIG='/host', OPENCODE_CONFIG_DIR='/host',
               OPENCODE_CONFIG_CONTENT='private', HERDCAT_TEST_TIMING='fast')
    result = self.run_tests([command, command], env=env)
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    paths = [json.loads(line) for line in result.stdout.splitlines()
        if line.startswith('"/tmp/')]
    self.assertEqual(len(set(paths)), 2)
    self.assertTrue(all(not Path(path).exists() for path in paths))

  def test_serial_order_exclusive_barrier_and_failures_are_reported(self):
    trace = self.root / 'trace'
    def command(name, code=0):
      return self.command(name, f"""
from pathlib import Path
import sys
trace = Path({str(trace)!r})
with trace.open('a') as out:
  out.write({name!r} + '\\n')
print('stdout-' + {name!r}, flush=True)
print('stderr-' + {name!r}, file=sys.stderr, flush=True)
sys.exit({code})
""")
    result = self.run_tests([command('first', 3), command('second')], jobs=1,
                exclusive=[command('exclusive', 4), command('last')])
    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
    self.assertEqual(trace.read_text().splitlines(),
            ['first', 'second', 'exclusive', 'last'])
    self.assertIn('stdout-first\nstderr-first', result.stdout)
    self.assertIn('Failed tests: first.py, exclusive.py', result.stdout)

  def test_exclusive_commands_never_overlap_parallel_commands(self):
    commands = []
    for name in ('alpha', 'beta'):
      commands.append(self.command(name, f"""
from pathlib import Path
import time
marker = Path({str(self.root / name)!r})
marker.touch()
time.sleep(.1)
marker.unlink()
marker.with_suffix('.done').touch()
"""))
    exclusive = self.command('exclusive', f"""
from pathlib import Path
root = Path({str(self.root)!r})
assert not (root / 'alpha').exists() and not (root / 'beta').exists()
assert (root / 'alpha.done').exists() and (root / 'beta.done').exists()
""")
    result = self.run_tests(commands, exclusive=[exclusive])
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

  def test_missing_command_does_not_abandon_other_tests(self):
    result = self.run_tests(['/nonexistent/herdcat-test',
                self.command('survivor', "print('survived')")])
    self.assertEqual(result.returncode, 1)
    self.assertIn('survived', result.stdout)
    self.assertIn('Failed tests: herdcat-test', result.stdout)

  def test_absolute_commands_work_with_a_fixture_only_path(self):
    command = self.command('private-path', "print('ran fixture')")
    result = self.run_tests([command], env=dict(os.environ, PATH=str(self.root)))
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    self.assertIn('ran fixture', result.stdout)

  def test_invalid_jobs_is_rejected_before_starting_tests(self):
    result = self.run_tests([self.command('unused', "raise AssertionError")], jobs=0)
    self.assertEqual(result.returncode, 2)
    self.assertIn('jobs must be positive', result.stderr)

  def test_output_without_final_newline_keeps_the_next_header_separate(self):
    commands = [self.command('partial', "print('partial', end='')"),
          self.command('next', "print('next')")]
    result = self.run_tests(commands, jobs=1)
    self.assertEqual(result.returncode, 0)
    self.assertIn('partial\n--- next.py ---', result.stdout)

  def test_job_limit_is_honored_with_more_commands_than_workers(self):
    commands = []
    for number in range(6):
      commands.append(self.command('worker' + str(number), f"""
from pathlib import Path
import time
root = Path({str(self.root)!r})
marker = root / 'active-{number}'
marker.touch()
assert len(list(root.glob('active-*'))) <= 2
time.sleep(.05)
marker.unlink()
"""))
    result = self.run_tests(commands)
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

  def test_make_serial_group_runs_each_binary_once(self):
    result = subprocess.run(
      ['make', '-n', 'test', 'TEST_BINARIES=build/test_file_limit',
      'TEST_EXCLUSIVE=build/test_file_limit',
      'TEST_FLAGS=' + str(self.root / 'flags')], cwd=RUNNER.parent.parent,
      capture_output=True, text=True, timeout=10)
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertEqual(result.stdout.count("'./build/test_file_limit'"), 1)
    self.assertIn("--exclusive './build/test_file_limit'", result.stdout)

  def test_drag_geometry_variants_are_parallel_candidates(self):
    result = subprocess.run(
      ['make', '-n', 'test-runtime',
      'TEST_FLAGS=' + str(self.root / 'flags')], cwd=RUNNER.parent.parent,
      capture_output=True, text=True, timeout=10)
    self.assertEqual(result.returncode, 0, result.stderr)
    for style in ('fan', 'post', 'off'):
      self.assertIn("--test 'python3 scripts/test_drag_runtime.py "
             + "--sign-style " + style + "'", result.stdout)

  def test_make_targets_use_the_scheduler_and_preserve_sanitizer_serial_mode(self):
    root = RUNNER.parent.parent
    # A nested make must not inherit this suite's command-line overrides.
    env = {key: value for key, value in os.environ.items()
       if key not in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES',
               'TEST_JOBS', 'TEST_SANITIZE')}
    flags = 'TEST_FLAGS=' + str(self.root / 'flags')
    for target in ('test', 'test-runtime'):
      result = subprocess.run(['make', '-n', target, 'TEST_JOBS=3', flags], cwd=root,
                  capture_output=True, text=True, timeout=10, env=env)
      self.assertEqual(result.returncode, 0, result.stderr)
      self.assertIn('scripts/run_tests.py --jobs 3', result.stdout)
    result = subprocess.run(['make', '-n', 'test', 'TEST_SANITIZE=1', flags], cwd=root,
                capture_output=True, text=True, timeout=10, env=env)
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertIn('scripts/run_tests.py --jobs 1', result.stdout)


if __name__ == '__main__':
  unittest.main()
