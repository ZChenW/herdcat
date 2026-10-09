"""Offline contracts for hook recording; never launch a real agent."""
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import shutil
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from record_agent_hooks import record, SHIM, capture_scope
from judge_agent_recording import judge
from update_agents import update_one
from agent_recording import Redactor, check_private, shape, compare_shapes, conclusion


class RedactionTests(unittest.TestCase):
    def test_preserves_shape_and_semantics_but_removes_private_values(self):
        original = {'hook_event_name': 'UserPromptSubmit', 'session_id': 'secret-id',
                    'cwd': '/home/testuser/work', 'prompt': 'private answer',
                    'input': {'command': 'echo private', 'empty': ''},
                    'output': ['private', 3, False, None], 'stopReason': 'end_turn'}
        clean = Redactor('/home/testuser', 'testuser').clean(original)
        self.assertEqual(clean['hook_event_name'], 'UserPromptSubmit')
        self.assertEqual(clean['prompt'], 'Sample prompt 1')
        self.assertEqual(clean['cwd'], '/tmp/herdcat-probe/project')
        self.assertEqual(clean['input'], {'command': 'Sample text', 'empty': ''})
        self.assertEqual(clean['output'], ['Sample text', 3, False, None])
        self.assertEqual(clean['stopReason'], 'end_turn')
        check_private(json.dumps(clean), '/home/testuser', 'testuser')

    def test_stable_ids_distinct_prompts_empty_strings_and_fail_closed(self):
        r = Redactor('/home/testuser', 'testuser')
        self.assertEqual(r.clean({'sessionId': 'one'}), r.clean({'sessionId': 'one'}))
        self.assertNotEqual(r.clean({'sessionId': 'one'}), r.clean({'sessionId': 'two'}))
        self.assertEqual(r.clean({'prompt': ''}), {'prompt': ''})
        self.assertEqual(r.clean({'prompt': 'two'}), {'prompt': 'Sample prompt 1'})
        for text in ['testuser', '/home/testuser', 'a@example.org', 'sk-' + 'X' * 48]:
            with self.assertRaises(ValueError):
                check_private(text, '/home/testuser', 'testuser')


class RecorderTests(unittest.TestCase):
    def test_exit_checkpoint_is_independent_of_the_presence_of_an_exit_hook(self):
        with tempfile.TemporaryDirectory(prefix='hc-exit-gap-') as tmp:
            script = Path(tmp) / 'fake.py'
            script.write_text("""import subprocess, sys
subprocess.run(['herdcat', '--hook', 'claude'], input='{"hook_event_name":"SessionStart","session_id":"one"}', text=True)
print('READY', flush=True)
for line in sys.stdin:
    if line.strip() == '/exit':
        break
""")
            recipe = {'agent': 'claude', 'command': [sys.executable, str(script)],
                      'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'ready': 'READY', 'start_event': 'SessionStart', 'timeout': 0.5,
                      'exit_keys': ['/exit', 'Enter'], 'scenarios': {'exit': {}}}
            output = Path(tmp) / 'out'
            self.assertEqual(record(recipe, 'exit', output, isolate=False), 'recorded')
            result = judge(output)
            self.assertEqual(result['conclusion'], 'needs-attention')
            self.assertIn('expected absent', result['failures'][0])


    def test_cleanup_hooks_after_last_milestone_do_not_change_scenario_shape(self):
        hooks = [{'ms': 10, 'event': 'SessionStart'}, {'ms': 20, 'event': 'Stop'},
                 {'ms': 30, 'event': 'SessionEnd'}]
        self.assertEqual(capture_scope(hooks, [{'ms': 25, 'state': 'done'}]), hooks[:2])


    def test_shim_is_quiet_and_copilot_receives_empty_policy_response(self):
        with tempfile.TemporaryDirectory(prefix='hc-shim-') as tmp:
            root = Path(tmp)
            shim = root / 'herdcat'
            shim.write_text(SHIM)
            shim.chmod(0o700)
            log = root / 'capture.jsonl'
            import os
            env = {**os.environ, 'HERDCAT_RECORD_LOG': str(log)}
            for agent, expected in [('claude', ''), ('copilot', '{}\n')]:
                result = subprocess.run([str(shim), '--hook', agent], input='{"x":"private"}',
                                        text=True, env=env, capture_output=True)
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stdout, expected)
                self.assertEqual(result.stderr, '')
            lines = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(lines[0]['stdin'], '{"x":"private"}')
            self.assertLessEqual(lines[0]['ms'], lines[1]['ms'])
            self.assertEqual(lines[1]['argv'], ['herdcat', '--hook', 'copilot'])

    def test_missing_initial_hook_fails_instead_of_accepting_empty_recording(self):
        with tempfile.TemporaryDirectory(prefix='hc-no-hook-') as tmp:
            recipe = {'agent': 'claude', 'command': [sys.executable, '-c',
                      'import time; print("READY", flush=True); time.sleep(10)'],
                      'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'ready': 'READY', 'start_event': 'SessionStart', 'timeout': 0.3,
                      'scenarios': {'exit': {}}}
            output = Path(tmp) / 'out'
            self.assertEqual(record(recipe, 'exit', output, isolate=False), 'needs-attention')
            self.assertFalse((output / 'provenance.json').exists())
            self.assertIn('empty recording rejected', (output / 'failure.json').read_text())
            self.assertIn('READY', (output / 'failure-screen.txt').read_text())

    def test_fake_agent_calls_shim_and_records_without_desktop_forwarding(self):
        with tempfile.TemporaryDirectory(prefix='hc-fake-') as tmp:
            script = Path(tmp) / 'fake.py'
            script.write_text("""import json, subprocess, sys

def hook(event, **extra):
    p = {'hook_event_name': event, 'session_id': 'real-private-id', **extra}
    subprocess.run(['herdcat', '--hook', 'claude'], input=json.dumps(p), text=True, check=True)
hook('SessionStart')
print('FAKE READY', flush=True)
for line in sys.stdin:
    if line.strip() == '/exit':
        hook('SessionEnd')
        break
    hook('UserPromptSubmit', prompt=line.strip())
    print('FAKE ANSWER ok', flush=True)
    hook('Stop')
""")
            recipe = {'agent': 'claude', 'command': [sys.executable, str(script)],
                      'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'ready': 'FAKE READY', 'answer': 'FAKE ANSWER ok',
                      'end_event': 'Stop', 'start_event': 'SessionStart',
                      'exit_keys': ['/exit', 'Enter'], 'timeout': 5,
                      'scenarios': {'normal': {'prompt': 'Reply with the single word ok'}}}
            output = Path(tmp) / 'out'
            with patch.dict('os.environ', {'HOME': tmp}):
                record(recipe, 'normal', output, isolate=False)
            self.assertTrue((output / '001.json').exists())
            self.assertEqual(json.loads((output / 'provenance.json').read_text())['recorded_version'], '1.2.3')
            self.assertIn('done', (output / 'expect.tsv').read_text())
            self.assertFalse(any(Path(tmp).glob('*.sock')))


class UpdateTests(unittest.TestCase):
    def test_partial_acceptance_keeps_bad_scene_and_whole_agent_validation_version(self):
        with tempfile.TemporaryDirectory(prefix='hc-partial-') as tmp:
            root = Path(tmp)
            fixtures = root / 'fixtures'
            fixtures.mkdir()
            (fixtures / 'manifest.json').write_text(json.dumps({'agents': [
                {'agent': 'claude', 'validated_version': '0.0.1',
                 'scenarios': [], 'recorded_versions': []}]}))
            recipe = {'agent': 'claude', 'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'scenarios': {'normal': {}, 'cancel': {}}}
            def recorder(recipe, scenario, output):
                output.mkdir()
                (output / 'provenance.json').write_text(json.dumps({'recorded_version': '1.2.3'}))
                (output / 'expect.tsv').write_text('recorded')
                return 'recorded'
            def judging(directory, against):
                return {'conclusion': 'new' if directory.name == 'normal' else 'needs-attention',
                        'agent': 'claude', 'failures': [], 'steps': 1, 'diff': None}
            result = update_one(recipe, root / 'out', fixtures=fixtures, no_upgrade=True,
                                accept=True, recorder=recorder, judge_fn=judging,
                                fixture_check=lambda: True)
            self.assertTrue(result['accepted'])
            self.assertTrue((fixtures / 'claude/normal/provenance.json').exists())
            self.assertFalse((fixtures / 'claude/cancel').exists())
            entry = json.loads((fixtures / 'manifest.json').read_text())['agents'][0]
            self.assertEqual(entry['validated_version'], '0.0.1')
            self.assertEqual(entry['recorded_versions'], ['1.2.3'])


    def test_failed_upgrade_continues_and_needs_attention_never_touches_fixtures(self):
        with tempfile.TemporaryDirectory(prefix='hc-upgrade-fake-') as tmp:
            root = Path(tmp)
            (root / 'manifest.json').write_text(json.dumps({'agents': [
                {'agent': 'claude', 'validated_version': '0.0.1'}]}))
            recipe = {'agent': 'claude', 'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'scenarios': {'normal': {}}}
            calls = []
            def updater(*args):
                calls.append('upgrade')
                return False
            def recorder(recipe, scenario, output):
                calls.append('record')
                output.mkdir()
                (output / 'provenance.json').write_text('{}')
                return 'recorded'
            def attention(*args):
                return {'conclusion': 'needs-attention', 'agent': 'claude',
                        'failures': ['independent milestone failed'], 'steps': 1, 'diff': None}
            original = (root / 'manifest.json').read_bytes()
            result = update_one(recipe, root / 'out', fixtures=root, accept=True,
                                upgrader=updater, recorder=recorder, judge_fn=attention)
            self.assertEqual(calls, ['upgrade', 'record'])
            self.assertIn('use installed version', result['upgrade'])
            self.assertEqual((root / 'manifest.json').read_bytes(), original)
            self.assertFalse((root / 'claude').exists())
            self.assertFalse(result['accepted'])

    def test_matching_version_skips_without_creating_outputs_or_upgrading(self):
        with tempfile.TemporaryDirectory(prefix='hc-same-') as tmp:
            root = Path(tmp)
            (root / 'manifest.json').write_text(json.dumps({'agents': [
                {'agent': 'claude', 'validated_version': '1.2.3'}]}))
            recipe = {'agent': 'claude', 'version_command': [sys.executable, '-c', 'print("1.2.3")']}
            def unexpected(*args):
                self.fail('matching version should have no side effects')
            result = update_one(recipe, root / 'out', fixtures=root,
                                recorder=unexpected, upgrader=unexpected)
            self.assertIn('skipped', result['status'])
            self.assertFalse((root / 'out').exists())


    def test_check_is_read_only_and_failed_acceptance_rolls_back(self):
        with tempfile.TemporaryDirectory(prefix='hc-update-') as tmp:
            root = Path(tmp)
            fixtures = root / 'fixtures'
            fixtures.mkdir()
            manifest = {'agents': [{'agent': 'claude', 'validated_version': '0.0.1',
                                    'scenarios': [], 'recorded_versions': []}]}
            (fixtures / 'manifest.json').write_text(json.dumps(manifest))
            recipe = {'agent': 'claude', 'version_command': [sys.executable, '-c', 'print("1.2.3")'],
                      'scenarios': {'normal': {}}}
            called = []
            def fake_record(recipe, scenario, output):
                called.append('record')
                output.mkdir(parents=True)
                (output / '001.json').write_text('{}')
                (output / 'provenance.json').write_text(json.dumps({'recorded_version': '1.2.3'}))
                return 'recorded'
            def fake_judge(*args):
                return {'agent': 'claude', 'conclusion': 'new', 'steps': 1, 'failures': [], 'diff': None}
            def fail_tests():
                called.append('test')
                return False
            options = dict(fixtures=fixtures, recorder=fake_record, judge_fn=fake_judge,
                           fixture_check=fail_tests)
            before = (fixtures / 'manifest.json').read_bytes()
            result = update_one(recipe, root / 'out', check=True, **options)
            self.assertEqual(called, [])
            self.assertFalse((root / 'out').exists())
            self.assertEqual(result['new_version'], '1.2.3')
            result = update_one(recipe, root / 'out', no_upgrade=True, accept=True, **options)
            self.assertEqual(result['scenarios']['normal'], 'needs-attention')
            self.assertEqual(called, ['record', 'test'])
            self.assertEqual((fixtures / 'manifest.json').read_bytes(), before)
            self.assertFalse((fixtures / 'claude').exists())


class ReplayTests(unittest.TestCase):
    def test_pi_process_identity_is_rebound_without_weakening_payload_types(self):
        with tempfile.TemporaryDirectory(prefix='hc-pi-replay-') as tmp:
            d = Path(tmp)
            (d / '001.json').write_text(json.dumps({'session_id': 'one', 'agent_pid': 424242}))
            (d / 'expect.tsv').write_text('0\t001.json\tsession_start\tidle\t-\t0\n')
            result = subprocess.run([str(ROOT / 'build/test_agent_fixtures'), '--dump', 'pi', tmp],
                                    text=True, capture_output=True, cwd=ROOT)
            self.assertEqual(result.stdout, '0\tidle\t-\t0\n')
            (d / '001.json').write_text(json.dumps({'session_id': 'one', 'agent_pid': '424242'}))
            invalid = subprocess.run([str(ROOT / 'build/test_agent_fixtures'), '--dump', 'pi', tmp],
                                     text=True, capture_output=True, cwd=ROOT)
            self.assertEqual(invalid.stdout, '0\tabsent\t-\t0\n')

    def test_real_hook_path_dumps_state_and_independent_gap_milestone(self):
        with tempfile.TemporaryDirectory(prefix='hc-replay-') as tmp:
            directory = Path(tmp)
            (directory / '001.json').write_text(json.dumps({
                'hook_event_name': 'SessionStart', 'session_id': 'fixture-one'}))
            (directory / '002.json').write_text(json.dumps({
                'hook_event_name': 'SessionEnd', 'session_id': 'fixture-one'}))
            (directory / 'expect.tsv').write_text(
                '0\t001.json\t-\tidle\t-\t0\n1\t-\t-\tdone\t-\t0\n'
                '2\t002.json\t-\tabsent\t-\t0\n')
            result = subprocess.run([str(ROOT / 'build/test_agent_fixtures'),
                                     '--dump', 'claude', tmp], text=True,
                                    capture_output=True, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(),
                             ['0\tidle\t-\t0', '1\tidle\t-\t0', '2\tabsent\t-\t0'])


class JudgeTests(unittest.TestCase):
    def test_judge_reports_failing_milestone_in_real_replay(self):
        with tempfile.TemporaryDirectory(prefix='hc-judge-') as tmp:
            d = Path(tmp)
            (d / '001.json').write_text(json.dumps({'hook_event_name': 'SessionStart', 'session_id': 'one'}))
            (d / 'provenance.json').write_text(json.dumps({'agent': 'claude', 'complete': True}))
            (d / 'expect.tsv').write_text('0\t001.json\t-\tidle\t-\t0\n1\t-\t-\tdone\t-\t0\n')
            result = judge(d)
            self.assertEqual(result['conclusion'], 'needs-attention')
            self.assertIn('expected done', result['failures'][0])
            (d / 'expect.tsv').write_text('0\t001.json\t-\tidle\t-\t0\n')
            self.assertEqual(judge(d)['conclusion'], 'new')

    def test_new_ignored_event_with_common_identity_is_compatible(self):
        policy = {'fields': {'session_id', 'hook_event_name'}, 'events': {'start'}}
        old = shape([('start', {'session_id': 'one', 'hook_event_name': 'start'})])
        new = shape([('start', {'session_id': 'one', 'hook_event_name': 'start'}),
                     ('irrelevant', {'session_id': 'one', 'hook_event_name': 'irrelevant'})])
        self.assertEqual(conclusion(compare_shapes(old, new, policy), True), 'compatible')

    def test_nested_types_and_sequence_with_repeated_events(self):
        result = shape([('start', {'x': {'y': [1, True, None]}}),
                        ('start', {}), ('stop', {'x': ''})])
        self.assertEqual(result['pattern'], ['start', 'stop'])
        self.assertEqual(result['fields']['start']['$.x.y[]'],
                         ['boolean', 'null', 'number'])

    def test_classification_uses_adapter_fields_and_events(self):
        old = shape([('start', {'session_id': 'one', 'extra': 1})])
        same = shape([('start', {'session_id': 'two', 'extra': 2})])
        harmless = shape([('start', {'session_id': 'two', 'extra': True})])
        changed = shape([('start', {'session_id': 42})])
        policy = {'fields': {'session_id'}, 'events': {'start'}}
        self.assertEqual(conclusion(compare_shapes(old, same, policy), True), 'unchanged')
        self.assertEqual(conclusion(compare_shapes(old, harmless, policy), True), 'compatible')
        self.assertEqual(conclusion(compare_shapes(old, changed, policy), True), 'needs-attention')
        self.assertEqual(conclusion(compare_shapes(old, same, policy), False), 'needs-attention')
        self.assertEqual(conclusion(None, True), 'new')
        added = shape([('start', {'session_id': 'one'}), ('finish', {})])
        self.assertTrue(compare_shapes(old, added, {'fields': set(), 'events': {'finish'}})['consumed_changed'])


if __name__ == '__main__':
    unittest.main()
