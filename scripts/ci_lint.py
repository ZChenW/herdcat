#!/usr/bin/env python3
"""Run the unchanged clang-tidy checks across four independent workers."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('sources', nargs='+')
    args = parser.parse_args()
    flags = shlex.split(os.environ['CI_LINT_FLAGS'])
    commands = [shlex.join(['clang-tidy', '--quiet', source, '--', *flags])
                for source in args.sources]
    invocation = [sys.executable, str(Path(__file__).with_name('run_tests.py')),
                  '--jobs', str(args.jobs)]
    if args.list:
        invocation.append('--list')
    for command in commands:
        invocation.extend(['--test', command])
    return subprocess.call(invocation)


if __name__ == '__main__':
    raise SystemExit(main())
