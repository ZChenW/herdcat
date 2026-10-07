#!/usr/bin/env python3
"""Check completions against the compiled adapter table and CLI help."""
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent


class CompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.agents = subprocess.check_output(
            [ROOT / 'build/test_agent_adapters', '--list-agents'],
            text=True).split()
        cls.files = {
            'bash': ROOT / 'completions/herdcat.bash',
            'zsh': ROOT / 'completions/_herdcat',
            'fish': ROOT / 'completions/herdcat.fish',
        }

    def test_agents_match_compiled_table(self):
        patterns = {
            'bash': r"local agents='([^']*)'",
            'zsh': r'agents=\(([^)]*)\)',
            'fish': r'^set -l agents (.*)$',
        }
        self.assertTrue(self.agents)
        for shell, path in self.files.items():
            with self.subTest(shell=shell):
                match = re.search(patterns[shell], path.read_text(), re.M)
                self.assertIsNotNone(match)
                self.assertCountEqual(match[1].split(), self.agents)

    def test_all_help_options_and_setup_targets(self):
        help_text = subprocess.check_output(
            [ROOT / 'build/herdcat', '--help'], text=True)
        options = set(re.findall(r'(?<!\w)--?[a-z][a-z-]*', help_text))
        for shell, path in self.files.items():
            with self.subTest(shell=shell):
                text = path.read_text()
                if shell == 'fish':
                    listed = set(re.findall(r'-l ([a-z-]+)', text))
                    listed |= set(re.findall(r'-s ([a-z])\b', text))
                    self.assertTrue(all(opt.lstrip('-') in listed
                                        for opt in options))
                    for name in ('status', 'dry-run', 'remove'):
                        self.assertIn('-n __herdcat_setup -l ' + name, text)
                else:
                    for option in options | {'--dry-run', '--remove'}:
                        self.assertIn(option, text)
                for target in ('setup', 'tmux', 'kitty'):
                    self.assertIn(target, text)

    def test_shell_syntax(self):
        for shell, path in self.files.items():
            with self.subTest(shell=shell):
                if not shutil.which(shell):
                    print(f'SKIP {shell} syntax: shell unavailable')
                    continue
                subprocess.run([shell, '-n', str(path)], check=True)

    def test_bash_contexts(self):
        if not shutil.which('bash'):
            self.skipTest('bash unavailable')
        for words, expected in [
                (['herdcat', '--hook', ''], self.agents),
                (['herdcat', 'setup', ''], self.agents + ['tmux', 'kitty']),
                (['herdcat', 'setup', '--remove', ''],
                 self.agents + ['tmux', 'kitty']),
                (['herdcat', 'setup', 'codex', ''], self.agents),
                (['herdcat', '--state', ''],
                 ['idle', 'working', 'waiting', 'done', 'error']),
                (['herdcat', '--pane', '123', ''], []),
                (['herdcat', 'setup', '--', ''], self.agents),
        ]:
            with self.subTest(words=words):
                result = subprocess.check_output([
                    'bash', '-c',
                    'source "$1"; shift; COMP_WORDS=("$@"); '
                    'COMP_CWORD=$((${#COMP_WORDS[@]}-1)); _herdcat; '
                    'printf "%s\\n" "${COMPREPLY[@]}"',
                    'completion-test', str(self.files['bash']), *words,
                ], text=True).split()
                self.assertTrue(set(expected) <= set(result))
                if not expected:
                    self.assertEqual(result, [])
                if words[1] == '--hook':
                    self.assertCountEqual(result, self.agents)

    def test_zsh_contexts(self):
        if not shutil.which('zsh'):
            self.skipTest('zsh unavailable')
        for words in (['herdcat', '--hook', ''],
                      ['herdcat', 'setup', ''],
                      ['herdcat', 'setup', '--remove', '']):
            with self.subTest(words=words):
                output = subprocess.check_output([
                    'zsh', '-fc',
                    'file=$1; shift; words=("$@"); '
                    '_arguments() { printf "%s\\n" "$@"; }; source "$file"',
                    'completion-test', str(self.files['zsh']), *words,
                ], text=True)
                for agent in self.agents:
                    self.assertIn(agent, output)
                if words[1] == 'setup':
                    self.assertIn('tmux kitty', output)


if __name__ == '__main__':
    unittest.main()
