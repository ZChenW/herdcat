#!/usr/bin/env python3
"""Check CI partitions against the complete, current Makefile test lists."""
import json
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / 'scripts/run_tests.py'


class ShardTests(unittest.TestCase):
    def setUp(self):
        root = tempfile.TemporaryDirectory(prefix='hc-ci-check-', dir='/tmp')
        self.addCleanup(root.cleanup)
        # GNU make evaluates $(file ...) even under -n, before mkdir recipes.
        # Keep dry-run flag writes private and independent of build/ existing.
        self.flags = 'TEST_FLAGS=' + str(Path(root.name) / 'flags')

    def test_documentation_changes_skip_heavy_work_but_code_changes_do_not(self):
        script = ROOT / 'scripts/ci_changes.py'
        cases = [(['README.md', 'docs/signs.md'], 'heavy=false'),
                 (['docs/design/example.svg'], 'heavy=false'),
                 (['src/core/main.c', 'README.md'], 'heavy=true'),
                 (['Makefile'], 'heavy=true'),
                 ([], 'heavy=true')]
        for paths, expected in cases:
            result = subprocess.run([sys.executable, str(script), '--paths', *paths],
                                    env={k: v for k, v in os.environ.items() if k != 'GITHUB_OUTPUT'},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected)

    def test_non_ci_builds_do_not_require_the_sharding_python_tools(self):
        with tempfile.TemporaryDirectory(prefix='hc-no-ci-', dir='/tmp') as tmp:
            tool = Path(tmp) / 'python3'
            tool.write_text('#!/bin/sh\necho UNEXPECTED_CI_PYTHON >&2\nexit 91\n')
            tool.chmod(0o700)
            result = subprocess.run(['make', '-n', 'release'], cwd=ROOT,
                                    env=dict(os.environ, PATH=tmp + ':' + os.environ['PATH']),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('UNEXPECTED_CI_PYTHON', result.stderr)

    def test_ci_sanitizer_build_is_optimized_and_keeps_both_sanitizers(self):
        result = subprocess.run(['make', '-B', '-n', 'ci-test-build', 'TEST_SANITIZE=1',
                                 'CI_SUITE=unit', 'CI_SHARD=1/1',
                                 'TEST_BINARIES=build/test_config', 'TEST_PYTHON=',
                                 'TEST_NODE=', self.flags], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        compile_line = next(line for line in result.stdout.splitlines()
                            if ' -c src/config/config.c ' in line and '/test/' in line)
        self.assertIn('-fsanitize=address,undefined', compile_line)
        self.assertIn('-O2', compile_line)
        self.assertIn('-DDEBUG -DTEST_BUILD', compile_line)
        self.assertNotIn('-DNDEBUG', compile_line)

    def test_python_drivers_build_their_c_fixtures_even_in_another_shard(self):
        result = subprocess.run([sys.executable, str(RUNNER), '--list-binaries',
                                 '--test', 'python3 tests/test_theme_watch.py',
                                 '--test', 'python3 tests/test_terminal_commands.py',
                                 '--test', 'python3 tests/test_completions.py',
                                 '--test', './build/test_theme_watch'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertCountEqual(result.stdout.split(), ['build/test_theme_watch',
                              'build/test_terminal_focus', 'build/test_agent_adapters', 'build/herdcat'])

    def test_product_binaries_are_built_only_for_the_drivers_that_use_them(self):
        result = subprocess.run([sys.executable, str(RUNNER), '--list-binaries',
                                 '--test', 'python3 tests/test_agy_hook_io.py',
                                 '--test', 'python3 scripts/test_setup.py',
                                 '--test', 'python3 scripts/test_status_hint.py',
                                 '--test', 'python3 tests/test_input_helper.py'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertCountEqual(result.stdout.split(), ['build/herdcat', 'build/herdcat-input'])

    def test_selected_build_prerequisites_match_selected_unit_commands(self):
        args = [sys.executable, str(RUNNER), '--shard', '1/2',
                '--test', './build/test_alpha', '--test', './build/test_beta',
                '--test', 'python3 scripts/example.py']
        result = subprocess.run([*args, '--list-binaries'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        selected = subprocess.run([*args, '--list'], capture_output=True, text=True)
        expected = [command.removeprefix('./') for command in json.loads(selected.stdout)
                    if command.startswith('./build/')]
        self.assertEqual(result.stdout.split(), expected)

    def test_ci_lint_covers_all_project_sources_once(self):
        result = subprocess.run(['make', '-n', 'lint'], cwd=ROOT,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        invocation = next(line for line in result.stdout.splitlines()
                          if line.startswith('clang-tidy --quiet'))
        tokens = shlex.split(invocation)
        split = tokens.index('--')
        sources, flags = tokens[2:split], tokens[split+1:]
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/ci_lint.py'),
                                 '--list', *sources], cwd=ROOT,
                                env=dict(os.environ, CI_LINT_FLAGS=shlex.join(flags)),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [shlex.split(command) for command in json.loads(result.stdout)]
        self.assertCountEqual([command[2] for command in commands], sources)
        self.assertEqual(len(commands), len(set(sources)))
        for command in commands:
            self.assertEqual(command[3:], ['--', *flags])
            self.assertNotIn('embedded_assets.c', command[2])
            self.assertTrue(command[2].startswith('src/'))

    def test_every_shard_covers_the_original_make_inventory_once(self):
        for suite, count, target in [('unit', 3, 'test'), ('runtime', 5, 'test-runtime')]:
            with self.subTest(suite=suite):
                complete = subprocess.run(['make', '-s', 'ci-test-list', f'CI_SUITE={suite}'],
                                          cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(complete.returncode, 0, complete.stderr)
                inventory = json.loads(complete.stdout)
                original = subprocess.run(['make', '-n', target, self.flags], cwd=ROOT,
                                          capture_output=True, text=True)
                self.assertEqual(original.returncode, 0, original.stderr)
                # Derive the reference from the existing local target, so
                # forgetting a new test in the CI argument list is detected.
                line = next(line for line in original.stdout.replace(chr(92) + '\n', '').splitlines()
                            if line.startswith('python3 scripts/run_tests.py --jobs'))
                tokens = shlex.split(line)
                reference = [tokens[i+1] for i, value in enumerate(tokens)
                             if value in ('--test', '--exclusive')]
                self.assertCountEqual(inventory['test'] + inventory['exclusive'], reference)
                collected = []
                for index in range(1, count + 1):
                    result = subprocess.run([sys.executable, str(RUNNER), '--list',
                                             '--shard', f'{index}/{count}', '--timings',
                                             str(ROOT / 'scripts/ci_test_timings.json'),
                                             *[arg for command in inventory['test']
                                               for arg in ('--test', command)],
                                             *[arg for command in inventory['exclusive']
                                               for arg in ('--exclusive', command)]],
                                            cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    collected.extend(json.loads(result.stdout))
                self.assertCountEqual(collected, reference)
                self.assertEqual(len(collected), len(set(collected)))

    def test_invalid_shards_and_duplicate_commands_fail_before_execution(self):
        for shard in ('0/3', '4/3', '1/0', '1', 'a/b'):
            result = subprocess.run([sys.executable, str(RUNNER), '--shard', shard,
                                     '--test', 'echo must-not-run'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertNotIn('must-not-run', result.stdout)
        result = subprocess.run([sys.executable, str(RUNNER), '--shard', '1/2',
                                 '--test', 'echo duplicate', '--exclusive', 'echo duplicate'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('duplicate test commands', result.stderr)

    def test_measured_weights_balance_work_and_keep_new_tests(self):
        with tempfile.TemporaryDirectory(prefix='hc-weights-', dir='/tmp') as tmp:
            path = Path(tmp) / 'times.json'
            path.write_text(json.dumps({'slow': 100, 'medium': 40, 'small': 30,
                                        'tiny': 20, 'short': 10}))
            args = [sys.executable, str(RUNNER), '--list', '--timings', str(path)]
            for command in ('slow', 'medium', 'small', 'tiny', 'short'):
                args.extend(['--test', command])
            groups = []
            for index in range(1, 4):
                result = subprocess.run([*args, '--shard', f'{index}/3'],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                groups.append(set(json.loads(result.stdout)))
            self.assertEqual(groups, [{'slow'}, {'medium', 'short'}, {'small', 'tiny'}])
            all_commands = []
            for index in range(1, 4):
                result = subprocess.run([*args, '--test', 'new', '--shard', f'{index}/3'],
                                        capture_output=True, text=True)
                all_commands.extend(json.loads(result.stdout))
            self.assertEqual(all_commands.count('new'), 1)


if __name__ == '__main__':
    unittest.main()
