#!/usr/bin/env python3
"""Run independent test commands with bounded concurrency and grouped output."""
import argparse
import json
import time

from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from ci_shards import build_targets, partition, read_timings, shard_number


def test_name(command):
  args = shlex.split(command)
  if Path(args[0]).name in ('python3', 'node') or 'python' in Path(args[0]).name:
    args = args[1:]
  return ' '.join([Path(args[0]).name, *args[1:]])


def run_test(command):
  with tempfile.TemporaryDirectory(prefix='hc-test-', dir='/tmp') as root, \
      tempfile.TemporaryFile(dir='/tmp') as output:
    env = dict(os.environ)
    for key in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'NIRI_SOCKET', 'SWAYSOCK',
          'HYPRLAND_INSTANCE_SIGNATURE', 'HERDCAT_HYPRLAND_NESTED',
          'DBUS_SESSION_BUS_ADDRESS', 'CLAUDE_PID', 'TMUX', 'TMUX_PANE',
          'KITTY_PID', 'KITTY_WINDOW_ID', 'KITTY_LISTEN_ON',
          'WEZTERM_PANE', 'WEZTERM_UNIX_SOCKET', 'CODEX_HOME',
          'KIMI_CODE_HOME', 'GROK_HOME', 'COPILOT_HOME', 'CLAUDE_CONFIG_DIR',
          'PI_CODING_AGENT_DIR', 'OPENCODE_CONFIG', 'OPENCODE_CONFIG_DIR',
          'OPENCODE_CONFIG_CONTENT', 'HERDCAT_TEST_TIMING'):
      env.pop(key, None)
    env.update(HOME=root, XDG_RUNTIME_DIR=root, TMPDIR=root,
         PYTHONDONTWRITEBYTECODE='1',
         GIT_OPTIONAL_LOCKS='0')
    for key in ('XDG_STATE_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME',
          'XDG_DATA_HOME'):
      env[key] = str(Path(root) / key.lower())
    result = subprocess.run(['/bin/sh', '-c', 'ulimit -c 0 && ulimit -n 1024 && exec "$@"',
                'test', *shlex.split(command)], env=env,
                stdout=output, stderr=subprocess.STDOUT)
    output.seek(0)
    return result.returncode, output.read()


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
  parser.add_argument('--test', action='append', default=[])
  parser.add_argument('--exclusive', action='append', default=[])
  parser.add_argument('--shard', help='duration-balanced, one-based shard i/n')
  parser.add_argument('--timings', help='JSON command-to-seconds weights')
  parser.add_argument('--list', action='store_true', help='print selected commands only')
  parser.add_argument('--list-binaries', action='store_true', help='print selected build targets')
  parser.add_argument('--list-groups', action='store_true', help='print the complete groups')
  parser.add_argument('--report', help='write measured command durations as JSON')
  args = parser.parse_args()
  if args.jobs < 1:
    parser.error('jobs must be positive')
  if args.shard:
    try:
      index, count = shard_number(args.shard)
      selected = set(partition(args.test + args.exclusive, count,
                               read_timings(args.timings))[index])
    except (ValueError, OSError) as error:
      parser.error(str(error))
    args.test = [command for command in args.test if command in selected]
    args.exclusive = [command for command in args.exclusive if command in selected]
  if args.list_binaries:
    print(' '.join(build_targets(args.test + args.exclusive)))
    return 0
  if args.list_groups:
    print(json.dumps(dict(test=args.test, exclusive=args.exclusive)))
    return 0
  if args.list:
    print(json.dumps(args.test + args.exclusive))
    return 0
  failures = []
  durations = {}

  def measured(command):
    start = time.monotonic()
    code, output = run_test(command)
    return code, output, time.monotonic() - start

  def report(command, result):
    code, output, seconds = result
    durations[command] = seconds
    print(f'--- {test_name(command)} ---', flush=True)
    print(output.decode(errors='replace'), end='', flush=True)
    if output and not output.endswith(b'\n'):
      print(flush=True)
    if code:
      failures.append(command)

  with ThreadPoolExecutor(max_workers=args.jobs) as pool:
    pending = {pool.submit(measured, command): command for command in args.test}
    for future in as_completed(pending):
      report(pending[future], future.result())
  # The pool has drained before any exclusive command starts.
  for command in args.exclusive:
    report(command, measured(command))
  if args.report:
    Path(args.report).write_text(json.dumps(durations, indent=2) + '\n')
  if failures:
    names = [test_name(command) for command in args.test + args.exclusive
        if command in failures]
    print('Failed tests: ' + ', '.join(names))
    return 1
  print('All tests passed.')
  return 0


if __name__ == '__main__':
  raise SystemExit(main())
