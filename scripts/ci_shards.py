"""Deterministic, duration-weighted CI partitions; unknown tests still run."""
import json
import math
from pathlib import Path


def partition(commands, count, timings):
    if len(commands) != len(set(commands)):
        raise ValueError('duplicate test commands in shard inventory')
    buckets = [[] for _ in range(count)]
    loads = [0.0] * count
    # A new test receives the largest measured weight, so adding a slow test
    # cannot silently overload the bucket used for inexpensive unmeasured work.
    fallback = max(timings.values(), default=1.0)
    ordered = sorted(commands, key=lambda c: (-timings.get(c, fallback), c))
    for command in ordered:
        index = min(range(count), key=lambda i: (loads[i], i))
        buckets[index].append(command)
        loads[index] += timings.get(command, fallback)
    return buckets


def read_timings(path):
    data = json.loads(Path(path).read_text()) if path else {}
    if not isinstance(data, dict) or any(
            not isinstance(value, (int, float)) or isinstance(value, bool)
            or not math.isfinite(value) or value <= 0 for value in data.values()):
        raise ValueError('timings must map test commands to positive finite seconds')
    return data


def shard_number(value):
    try:
        index, count = map(int, value.split('/'))
    except ValueError as error:
        raise ValueError('shard must be i/n (1 <= i <= n)') from error
    if not 1 <= index <= count:
        raise ValueError('shard must be i/n (1 <= i <= n)')
    return index - 1, count


def build_targets(commands):
    """Include binaries used by Python drivers without running extra suites."""
    fixtures = {
        'python3 tests/test_agent_recording.py': ['build/test_agent_fixtures'],
        'python3 tests/test_theme_watch.py': ['build/test_theme_watch'],
        'python3 tests/test_terminal_commands.py': ['build/test_terminal_focus'],
        'python3 tests/test_completions.py': ['build/test_agent_adapters', 'build/herdcat'],
        'python3 tests/test_agy_hook_io.py': ['build/herdcat'],
        'python3 scripts/test_setup.py': ['build/herdcat'],
        'python3 scripts/test_status_hint.py': ['build/herdcat'],
        'python3 tests/test_input_helper.py': ['build/herdcat-input'],
    }
    targets = []
    for command in commands:
        binary = command.split()[0]
        if binary.startswith('./build/'):
            targets.append(binary.removeprefix('./'))
        if command in fixtures:
            targets.extend(fixtures[command])
    return list(dict.fromkeys(targets))
