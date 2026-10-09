#!/usr/bin/env python3
"""Replay hooks through production code and compare adapter-visible shapes."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from agent_recording import ROOT, shape, compare_shapes, conclusion, recording_records, write_json


def replay_binary():
    binary = ROOT / 'build/test_agent_fixtures'
    if binary.exists() and int(os.environ.get('MAKELEVEL', '0')) > 0:
        return binary
    subprocess.run(['make', '--no-print-directory', 'build/test_agent_fixtures'],
                   cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
    return binary


def adapter_policy(agent, binary):
    result = subprocess.run([str(binary), '--adapter', agent], cwd=ROOT,
                            capture_output=True, text=True, check=True, timeout=10)
    policy = {'fields': set(), 'events': set()}
    for line in result.stdout.splitlines():
        kind, name = line.split('\t')
        policy[kind + 's'].add(name)
    return policy


def judge(directory, against=None):
    directory = Path(directory)
    provenance = json.loads((directory / 'provenance.json').read_text())
    agent = provenance.get('agent', directory.parent.name)
    binary = replay_binary()
    result = subprocess.run([str(binary), '--dump', agent, str(directory.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    expected = [line.split('\t') for line in (directory / 'expect.tsv').read_text().splitlines()
                if line and not line.startswith('#')]
    actual = [line.split('\t') for line in result.stdout.splitlines()]
    failures = []
    if result.returncode:
        failures.append(f'replay exited {result.returncode}: {result.stderr[-2000:]}')
    if len(actual) != len(expected):
        failures.append(f'replay returned {len(actual)} steps; expected {len(expected)}')
    for index, (want, got) in enumerate(zip(expected, actual), 1):
        expected_row = [want[0], *want[3:]]
        if expected_row != got:
            failures.append(f'step {index} ({want[1]}, {want[0]} ms): expected '
                            f'{want[3]} title={want[4]!r} subagent={want[5]}; actual ' + '\t'.join(got[1:]))
    if not provenance.get('complete', True) or (directory / 'failure.json').exists():
        failures.append('recording did not complete its script; see failure.json and failure-screen.txt')
    if not expected or not any(row[1] != '-' for row in expected):
        failures.append('empty recording is not evidence')
    diff = None
    policy = adapter_policy(agent, binary)
    if against:
        old_agent = json.loads((Path(against) / 'provenance.json').read_text()).get('agent', Path(against).parent.name)
        if old_agent != agent:
            raise ValueError('cannot compare recordings from different agents')
        diff = compare_shapes(shape(recording_records(against)),
                              shape(recording_records(directory)), policy)
    return {'conclusion': conclusion(diff, not failures), 'agent': agent,
            'failures': failures, 'diff': diff, 'steps': len(expected),
            'actual': actual, 'adapter_fields': sorted(policy['fields']),
            'adapter_events': sorted(policy['events'])}


def markdown(result):
    lines = [f"**{result['agent']}: {result['conclusion']}** ({result['steps']} replay steps)"]
    lines.extend('- ' + failure for failure in result['failures'])
    if result['diff']:
        for change in result['diff']['changes']:
            lines.append('- ' + change['kind'] + ': ' + change.get('event', '') +
                         ' ' + change.get('path', '') +
                         (' **adapter reads this**' if change['consumed'] else ' (not consumed)'))
    if not result['failures']:
        lines.append('All authored milestones, titles and child markers passed.')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--against', type=Path)
    args = parser.parse_args()
    try:
        result = judge(args.directory, args.against)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print(f'needs-attention: {exc}', file=sys.stderr)
        return 2
    print(markdown(result), end='')
    return 2 if result['conclusion'] == 'needs-attention' else 0


if __name__ == '__main__':
    sys.exit(main())
