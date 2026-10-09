#!/usr/bin/env python3
"""Offline version-monitor regression tests; never launch an agent."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'agent_versions', ROOT / 'scripts/check_agent_versions.py')
versions = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(versions)
RESPONSES = ROOT / 'tests/agent_version_responses'


class AgentVersionsTests(unittest.TestCase):
    def test_response_formats(self):
        for kind, version in [('npm', '0.26.0'), ('pypi', '2.2.0'),
                              ('github', '2.2.0')]:
            with self.subTest(kind=kind):
                data = json.loads((RESPONSES / (kind + '.json')).read_text())
                self.assertEqual(versions.parse_response(kind, data), version)

    def test_invalid_response(self):
        for kind in ('npm', 'pypi', 'github'):
            for data in ({}, [], None, {'info': {'version': 2}},
                         {'version': 'garbage'},
                         {'tag_name': 'v2.2.0', 'prerelease': True},
                         {'tag_name': 'v2.2.0', 'draft': True}):
                with self.subTest(kind=kind, data=data):
                    with self.assertRaises(ValueError):
                        versions.parse_response(kind, data)

    def test_version_order(self):
        for latest, old, expected in [('0.26.0', '0.25.0', True),
                                     ('v0.25.0', '0.25.0', False),
                                     ('0.24.0', '0.25.0', False),
                                     ('0.25.0-rc.1', '0.25.0', False),
                                     ('0.25.0', '0.25.0-rc.1', True),
                                     ('0.25.0-rc.10', '0.25.0-rc.2', True),
                                     ('0.25.0+build', '0.25.0', False)]:
            self.assertEqual(versions.is_newer(latest, old), expected)

    def test_manifest(self):
        manifest = versions.load_manifest(ROOT / 'tests/agent_fixtures')
        self.assertEqual({row['agent'] for row in manifest}, {
            'claude', 'codex', 'grok', 'kimi', 'cursor', 'copilot', 'pi',
            'opencode', 'qwen', 'agy'})
        for agent in manifest:
            for scenario in agent['scenarios']:
                provenance = json.loads((ROOT / 'tests/agent_fixtures' /
                                         agent['agent'] / scenario /
                                         'provenance.json').read_text())
                self.assertIn(provenance['recorded_version'],
                              agent['recorded_versions'])
                directory = (ROOT / 'tests/agent_fixtures' /
                             agent['agent'] / scenario)
                rows = [line.split('\t') for line in
                        (directory / 'expect.tsv').read_text().splitlines()
                        if not line.startswith('#')]
                self.assertEqual(len(rows), len(provenance['steps']))
                self.assertEqual({row[1] for row in rows},
                                 {p.name for p in directory.glob('[0-9]*.json')})
                last_time = -1
                last_source = 0
                for row, step in zip(rows, provenance['steps']):
                    self.assertEqual(len(row), 6)
                    self.assertGreaterEqual(int(row[0]), last_time)
                    self.assertGreater(step['source_line'], last_source)
                    last_time, last_source = int(row[0]), step['source_line']
                    self.assertEqual(row[1], step['payload'])
                    self.assertEqual(step['argv'], ['herdcat', '--hook', agent['agent']] +
                                     ([] if row[2] == '-' else ['--event', row[2]]))
                    self.assertIn(row[3], ('idle', 'working', 'waiting', 'done',
                                          'error', 'absent'))
                    self.assertIn(row[5], ('0', '1'))
                    self.assertIsInstance(json.loads((directory / row[1]).read_text()), dict)


    def test_network_failure_is_not_update(self):
        agent = {'agent': 'qwen', 'recorded_versions': ['0.25.0'],
                 'scenarios': ['normal'], 'query': {
                     'kind': 'npm', 'url': 'https://registry.npmjs.org/example'}}
        with patch.object(versions, 'fetch_response', side_effect=OSError('offline')):
            row = versions.check_agent(agent)
        self.assertEqual(row['status'], 'unavailable')
        self.assertIsNone(row['latest'])

    def test_multiple_baselines_and_missing_payload(self):
        agent = {'agent': 'qwen', 'recorded_versions': ['0.25.0', '0.24.0'],
                 'scenarios': ['normal'], 'query': {'kind': 'npm', 'url': 'unused'}}
        with patch.object(versions, 'fetch_response', return_value={
                'version': '0.25.0'}):
            self.assertEqual(versions.check_agent(agent)['status'], 'update')
            agent['recorded_versions'] = []
            agent['scenarios'] = []
            self.assertEqual(versions.check_agent(agent)['status'], 'missing')

    def test_summary_and_exit_status(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            root = Path(directory)
            (root / 'qwen.json').write_text((RESPONSES / 'npm.json').read_text())
            summary = root / 'summary.md'
            out = io.StringIO()
            with patch.dict('os.environ', {'GITHUB_STEP_SUMMARY': str(summary)}), \
                    contextlib.redirect_stdout(out):
                self.assertEqual(versions.main(['--responses', str(root),
                                                '--fail-on-update']), 1)
            self.assertIn('qwen', summary.read_text())
            self.assertIn('observe its hooks again', summary.read_text())
            self.assertIn('hand-written', summary.read_text())
            self.assertIn('cannot be looked up', out.getvalue())
            self.assertIn('none', out.getvalue())
            (root / 'qwen.json').write_text('invalid JSON')
            with patch.dict('os.environ', {'GITHUB_STEP_SUMMARY': ''}), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(versions.main(['--responses', str(root),
                                                '--fail-on-update']), 0)


if __name__ == '__main__':
    unittest.main()
