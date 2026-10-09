#!/usr/bin/env python3
"""Compare recorded hook versions with public registries, without installing agents.

Each query and its provenance live in tests/agent_fixtures/manifest.json.
Network/format failures are informational. --fail-on-update fails only when a
successfully parsed latest version is newer than the version an agent was
last checked against (its fixtures, or validated_version without any).
"""
import argparse
import json
import os
from pathlib import Path
import re
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r'v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?'
                     r'(?:\+[0-9A-Za-z.-]+)?\Z')


def version_key(value):
    if not isinstance(value, str):
        raise ValueError('version must be a string')
    match = VERSION.fullmatch(value)
    if not match:
        raise ValueError('unsupported version format')
    core = tuple(int(part) for part in match.groups()[:3])
    prerelease = match[4]
    tail = ((1,),) if prerelease is None else ((0,), tuple(
        (0, int(part)) if part.isdigit() else (1, part)
        for part in prerelease.split('.')))
    return core, tail


def is_newer(latest, recorded):
    return version_key(latest) > version_key(recorded)


def parse_response(kind, data):
    try:
        if kind == 'npm':
            value = data['version']
        elif kind == 'pypi':
            value = data['info']['version']
        elif kind == 'github':
            if data['draft'] is not False or data['prerelease'] is not False:
                raise ValueError('not a stable published release')
            # Monorepo tags look like "name@1.2.3" or "@scope/name@1.2.3".
            value = data['tag_name'].rsplit('@', 1)[-1]
        else:
            raise ValueError('unknown registry')
        version_key(value)
        return value.removeprefix('v')
    except (KeyError, TypeError) as error:
        raise ValueError('invalid registry response') from error


def load_manifest(root):
    data = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    agents = data['agents']
    if len({agent['agent'] for agent in agents}) != len(agents):
        raise ValueError('duplicate agent in manifest')
    for agent in agents:
        recorded = set()
        actual = {path.parent.name
                  for path in (root / agent['agent']).glob('*/expect.tsv')}
        if actual != set(agent['scenarios']):
            raise ValueError('scenario inventory mismatch: ' + agent['agent'])
        for scenario in agent['scenarios']:
            source = json.loads((root / agent['agent'] / scenario /
                                 'provenance.json').read_text(encoding='utf-8'))
            version_key(source['recorded_version'])
            recorded.add(source['recorded_version'])
        if recorded != set(agent['recorded_versions']):
            raise ValueError('recorded-version mismatch: ' + agent['agent'])
    return agents


def fetch_response(url):
    request = urllib.request.Request(url, headers={
        'User-Agent': 'herdcat-hook-version-monitor',
        'Accept': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise ValueError('registry response exceeds 1 MiB')
        return json.loads(raw)


def baselines(agent):
    """Versions this agent's hooks were last checked against."""
    known = list(agent['recorded_versions'])
    if not known and agent.get('validated_version'):
        known.append(agent['validated_version'])
    return known


def check_agent(agent, responses=None):
    row = dict(agent, latest=None, status='unavailable', detail='')
    query = agent['query']
    if not query:
        row['detail'] = 'cannot be looked up: ' + agent['query_note']
        return row
    try:
        data = (json.loads((responses / (agent['agent'] + '.json')).read_text())
                if responses is not None else fetch_response(query['url']))
        row['latest'] = parse_response(query['kind'], data)
        known = baselines(agent)
        row['status'] = ('missing' if not known else
                         'update' if any(is_newer(row['latest'], old)
                                         for old in known) else
                         'current')
    except (OSError, ValueError, TimeoutError) as error:
        # Do not print response bodies, environment variables or credentials.
        row['detail'] = 'lookup failed: ' + type(error).__name__
    return row


def render(rows, offline=False):
    lines = ['# Agent versions', '',
             ('Source: hand-written responses (offline parsing test, not a '
              'lookup).' if offline else
              'Source: public registries and GitHub Releases. No agent was '
              'run, installed or upgraded.'), '',
             '| Agent | Checked against | Fixtures | Latest | Result | Lookup |',
             '| --- | --- | --- | --- | --- | --- |']
    for row in rows:
        known = ', '.join(baselines(row)) or 'none'
        status = {'update': '**newer release: observe its hooks again**',
                  'current': 'up to date',
                  'missing': 'no checked version on record',
                  'unavailable': row['detail']}[row['status']]
        query = row['query']
        method = f"[{query['kind']}]({query['url']})" if query else 'none'
        lines.append(f"| {row['agent']} | {known} "
                     f"| {len(row['scenarios']) or 'none'} "
                     f"| {row['latest'] or 'unknown'} | {status} | {method} |")
    lines += ['', 'A newer release only asks for its hooks to be observed '
              'again; nothing is re-recorded or changed automatically.',
              'Network errors, rate limits and unexpected responses are '
              'reported as unknown and never fail the run.']
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', type=Path, default=ROOT / 'tests/agent_fixtures')
    parser.add_argument('--responses', type=Path,
                        help='offline hand-authored JSON responses named <agent>.json')
    parser.add_argument('--fail-on-update', action='store_true')
    args = parser.parse_args(argv)
    rows = [check_agent(agent, args.responses) for agent in load_manifest(args.fixtures)]
    summary = render(rows, offline=args.responses is not None)
    print(summary, end='')
    if path := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(path, 'a', encoding='utf-8') as output:
            output.write(summary)
    updated = [row for row in rows if row['status'] == 'update']
    if os.environ.get('GITHUB_ACTIONS'):
        # A yellow note on the run; a weekly job that is always red is ignored.
        for row in updated:
            print(f"::warning title={row['agent']} {row['latest']}::hooks were "
                  f"last checked against {', '.join(baselines(row))}")
    return int(args.fail_on_update and bool(updated))


if __name__ == '__main__':
    raise SystemExit(main())
