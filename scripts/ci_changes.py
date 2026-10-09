#!/usr/bin/env python3
"""Keep required checks green for documentation-only changes; fail open to CI."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def changed_paths():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request':
        # actions/checkout checks out the merge commit: its first parent is
        # the target branch, so unrelated target changes are not counted.
        before = 'HEAD^1'
    else:
        before = event.get('before', '')
        if not before or set(before) == {'0'}:
            return []
    result = subprocess.run(['git', 'diff', '--no-renames', '--name-only', '-z',
                             before, 'HEAD'], check=True, capture_output=True)
    return result.stdout.decode().rstrip('\0').split('\0') if result.stdout else []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--paths', nargs='*', help='explicit paths for offline checks')
    args = parser.parse_args()
    try:
        paths = changed_paths() if args.paths is None else args.paths
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError):
        paths = []  # Unknown ranges, first pushes and empty changes run all checks.
    docs_only = bool(paths) and all(path.startswith('docs/') or path.endswith('.md')
                                   for path in paths)
    line = 'heavy=' + ('false' if docs_only else 'true')
    print(line)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            output.write(line + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
