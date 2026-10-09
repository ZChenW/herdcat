#!/usr/bin/env python3
"""Check/upgrade, record, judge and transactionally accept agent fixtures."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from agent_recording import ROOT, write_json
from record_agent_hooks import load_recipe, installed_version, record
from judge_agent_recording import judge, markdown


def accept_recordings(agent, recordings, fixtures, version, scenarios, fixture_check, complete=True):
    """Rollback this agent and the manifest if production replay rejects it."""
    manifest_path = fixtures / 'manifest.json'
    original_manifest = manifest_path.read_bytes()
    destination = fixtures / agent
    with tempfile.TemporaryDirectory(prefix='hc-accept-', dir='/tmp') as tmp:
        backup = Path(tmp) / 'agent'
        existed = destination.exists()
        if existed:
            shutil.copytree(destination, backup)
        try:
            destination.mkdir(exist_ok=True)
            for scenario in scenarios:
                target = destination / scenario
                if target.exists():
                    shutil.rmtree(target)
                target.mkdir()
                source = recordings / scenario
                for item in source.iterdir():
                    if item.name in {'expect.tsv', 'provenance.json'} or (item.suffix == '.json' and item.stem.isdigit()):
                        shutil.copyfile(item, target / item.name)
            manifest = json.loads(original_manifest)
            entry = next(e for e in manifest['agents'] if e['agent'] == agent)
            if complete:
                entry['validated_version'] = version
            entry.update(scenarios=sorted(p.name for p in destination.iterdir() if p.is_dir()),
                         evidence_note='Captured by record_agent_hooks.py from the real agent; see each provenance.json.' +
                         ('' if complete else ' Partial validation: needs-attention scenes were not accepted; validated_version was retained.'))
            entry['recorded_versions'] = sorted({
                json.loads((p / 'provenance.json').read_text())['recorded_version']
                for p in destination.iterdir() if p.is_dir()})
            write_json(manifest_path, manifest)
            if not fixture_check():
                raise RuntimeError('make test-agent-fixtures failed; acceptance rolled back')
        except BaseException:
            if destination.exists():
                shutil.rmtree(destination)
            if existed:
                shutil.copytree(backup, destination)
            manifest_path.write_bytes(original_manifest)
            raise


def check_fixtures():
    return subprocess.run(['make', 'test-agent-fixtures'], cwd=ROOT).returncode == 0


def upgrade(recipe, output):
    # Updaters only: never make an authenticated model call here.
    command = recipe['upgrade']
    original_env = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='hc-upgrade-', dir='/tmp') as tmp:
        root = Path(tmp)
        (root / '.git').mkdir()
        (root / 'home').mkdir(mode=0o700)
        env = original_env.copy()
        env.update(HOME=str(root / 'home'), XDG_RUNTIME_DIR=str(root),
                   XDG_CONFIG_HOME=str(root / 'home/.config'),
                   XDG_CACHE_HOME=str(root / 'home/.cache'),
                   XDG_DATA_HOME=str(root / 'home/.local/share'),
                   CLAUDE_CONFIG_DIR=str(root / 'home/.claude'),
                   CODEX_HOME=str(root / 'home/.codex'),
                   COPILOT_HOME=str(root / 'home/.copilot'),
                   PI_CODING_AGENT_DIR=str(root / 'home/.pi/agent'))
        config = recipe.get('config', {})
        if config.get('env'):
            env[config['env']] = str(root / 'home' / config['directory'])
        for key in ['WAYLAND_DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'TMUX', 'TMUX_PANE']:
            env.pop(key, None)
        # Keep npm's actual installation prefix while moving its cache/logs to
        # /tmp. This is the explicit global-package exception in PLAN.md.
        prefix = subprocess.run(['npm', 'prefix', '-g'], env=original_env,
                                capture_output=True, text=True, timeout=15, check=True).stdout.strip()
        env['npm_config_prefix'] = prefix
        env['npm_config_cache'] = str(root / 'npm-cache')
        result = subprocess.run(command, env=env, cwd=root, capture_output=True,
                                text=True, timeout=180)
        diagnostics = result.stdout + result.stderr
        (output / 'upgrade.log').write_text(diagnostics)
        return result.returncode == 0


def update_one(recipe, output, check=False, no_upgrade=False, accept=False,
               force=False, fixtures=None, recorder=record, judge_fn=judge,
               fixture_check=check_fixtures, upgrader=upgrade):
    fixtures = Path(fixtures or ROOT / 'tests/agent_fixtures')
    output = Path(output)
    manifest = json.loads((fixtures / 'manifest.json').read_text())
    entry = next(e for e in manifest['agents'] if e['agent'] == recipe['agent'])
    installed, _ = installed_version(recipe)
    old = entry.get('validated_version') or ','.join(entry.get('recorded_versions', []))
    summary = {'agent': recipe['agent'], 'old_version': old, 'new_version': installed,
               'scenarios': {}, 'accepted': False}
    if check:
        summary['status'] = 'unchanged' if installed == old else 'recording-required'
        return summary
    if installed == old and not force:
        summary['status'] = 'version-matches; skipped'
        return summary
    output.mkdir(parents=True, exist_ok=True)
    if not no_upgrade:
        try:
            summary['upgrade'] = 'success' if upgrader(recipe, output) else 'failed; use installed version'
        except (OSError, subprocess.SubprocessError) as exc:
            summary['upgrade'] = f'failed ({type(exc).__name__}); use installed version'
        installed, _ = installed_version(recipe)
        summary['new_version'] = installed
    safe = []
    for scenario in recipe['scenarios']:
        directory = output / scenario
        try:
            recorded = recorder(recipe, scenario, directory)
            if recorded == 'skipped':
                summary['scenarios'][scenario] = 'skipped: ' + recipe['scenarios'][scenario]['skip']
                continue
            if not (directory / 'provenance.json').exists():
                summary['scenarios'][scenario] = 'needs-attention'
                continue
            baseline = fixtures / recipe['agent'] / scenario
            result = judge_fn(directory, baseline if baseline.exists() else None)
            if recorded != 'recorded':
                result['conclusion'] = 'needs-attention'
            write_json(directory / 'judgement.json', result)
            (directory / 'judgement.md').write_text(markdown(result))
            summary['scenarios'][scenario] = result['conclusion']
            if result['conclusion'] != 'needs-attention':
                safe.append(scenario)
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'orchestration-error.txt').write_text(str(exc))
            summary['scenarios'][scenario] = 'needs-attention'
        print(f"{recipe['agent']}/{scenario}: {summary['scenarios'][scenario]}", flush=True)
    failed = 'needs-attention' in summary['scenarios'].values()
    if accept and safe:
        try:
            accept_recordings(recipe['agent'], output, fixtures, installed, safe, fixture_check, complete=not failed)
            summary['accepted'] = True
            summary['accepted_scenarios'] = safe
            summary['fully_validated'] = not failed
        except (RuntimeError, OSError) as exc:
            summary['acceptance_error'] = str(exc)
            for scenario in safe:
                summary['scenarios'][scenario] = 'needs-attention'
    write_json(output / 'summary.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('agents', nargs='*', default=['claude', 'codex', 'copilot', 'pi'])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--no-upgrade', action='store_true')
    parser.add_argument('--accept', action='store_true')
    parser.add_argument('--force', action='store_true', help='record even when installed version is already validated')
    parser.add_argument('--out', type=Path, help='output directory (default: new private /tmp directory)')
    args = parser.parse_args()
    output = args.out
    if not args.check and output is None:
        output = Path(tempfile.mkdtemp(prefix='hc-agent-update-', dir='/tmp'))
    summaries = []
    failed = False
    for agent in args.agents:
        try:
            summary = update_one(load_recipe(agent), (output / agent) if output else Path('/tmp/unused'),
                                 check=args.check, no_upgrade=args.no_upgrade, accept=args.accept, force=args.force)
            summaries.append(summary)
            failed |= 'needs-attention' in summary['scenarios'].values()
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            print(f'{agent}: needs-attention ({exc})', file=sys.stderr)
            failed = True
    print('\n| Agent | Validated | Installed after update | Scenario conclusions |')
    print('| --- | --- | --- | --- |')
    for item in summaries:
        results = '; '.join(f'{k}: {v}' for k, v in item['scenarios'].items()) or item.get('status', '')
        print(f"| {item['agent']} | {item['old_version']} | {item['new_version']} | {results} |")
    if output:
        write_json(output / 'summary.json', summaries)
        print(f'Recordings and diagnostics: {output}')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
