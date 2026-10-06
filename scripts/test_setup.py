#!/usr/bin/env python3
"""Regression tests for setup; all configuration access uses temporary HOME."""
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / 'scripts/herdcat-setup'
FIXTURES = ROOT / 'tests/setup_fixtures'
loader = importlib.machinery.SourceFileLoader('herdcat_setup', str(SCRIPT))
spec = importlib.util.spec_from_loader(loader.name, loader)
setup = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = setup
loader.exec_module(setup)
SECRET = b'sk-FAKE-DO-NOT-USE'
TARGETS = {
    'claude': '.claude/settings.json', 'codex': '.codex/hooks.json',
    'grok': '.grok/hooks/herdcat.json', 'cursor': '.cursor/hooks.json',
    'copilot': '.copilot/hooks/hooks.json', 'kimi': '.kimi-code/config.toml',
    'pi': '.pi/agent/extensions/herdcat.ts',
    'opencode': '.config/opencode/opencode.jsonc',
}
TEMPLATES = {
    'claude': 'hooks/claude-code.settings.json', 'codex': 'hooks/codex.hooks.json',
    'grok': 'hooks/grok.json', 'cursor': 'hooks/cursor.hooks.json',
    'copilot': 'hooks/copilot.hooks.json', 'kimi': 'hooks/kimi-code.toml',
    'pi': 'pi/herdcat.ts', 'opencode': 'opencode/index.js',
}


def snapshot(directory):
    result = {}
    for path in directory.rglob('*'):
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            value = ('symlink', os.readlink(path), mode)
        elif path.is_dir():
            value = ('directory', mode)
        else:
            value = ('file', path.read_bytes(), mode)
        result[str(path.relative_to(directory))] = value
    return result


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='herdcat-setup-test-')
        self.home = Path(self.temporary.name)
        self.env = dict(os.environ, HOME=str(self.home),
                        XDG_CONFIG_HOME=str(self.home / '.config'),
                        XDG_STATE_HOME=str(self.home / '.local/state'),
                        XDG_CACHE_HOME=str(self.home / '.cache'),
                        XDG_DATA_HOME=str(self.home / '.local/share'),
                        XDG_RUNTIME_DIR=str(self.home / 'runtime'),
                        LC_MESSAGES='en_US.UTF-8', LANG='en_US.UTF-8')
        for key in ('CODEX_HOME', 'GROK_HOME', 'CLAUDE_CONFIG_DIR', 'COPILOT_HOME',
                    'KIMI_CODE_HOME', 'PI_CODING_AGENT_DIR', 'OPENCODE_CONFIG',
                    'OPENCODE_CONFIG_DIR', 'OPENCODE_CONFIG_CONTENT'):
            self.env.pop(key, None)
        self.bin = self.home / 'bin'
        self.bin.mkdir()
        (self.bin / 'python3').symlink_to(sys.executable)
        self.env['PATH'] = str(self.bin)

    def tearDown(self):
        self.temporary.cleanup()

    def run_setup(self, *args, code=0, env=None):
        result = subprocess.run([sys.executable, str(SCRIPT), *args],
                                env=env or self.env, input=b'', capture_output=True,
                                cwd=self.home, timeout=10)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        self.assertNotIn(SECRET, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def write(self, path, content, mode=0o640):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        path.chmod(mode)

    def fixture(self, agent, variant):
        suffix = 'toml' if agent == 'kimi' else 'ts' if agent == 'pi' else 'jsonc' if agent == 'opencode' else 'json'
        content = (FIXTURES / agent / (variant + '.' + suffix)).read_bytes()
        plugin = self.home / '.config/opencode/herdcat-plugin'
        content = content.replace(b'@HERDCAT_PLUGIN@', str(plugin).encode())
        path = self.home / TARGETS[agent]
        if agent == 'pi' and variant != 'old':
            path.parent.mkdir(parents=True, exist_ok=True)
            if variant == 'other':
                self.write(path.with_name('audit.ts'), content)
            return path, None
        self.write(path, content, 0o600 if agent == 'kimi' else 0o640)
        if agent == 'opencode' and variant == 'old':
            self.write(plugin / 'index.js', (FIXTURES / agent / 'old-index.js').read_bytes())
        return path, content

    def expected(self, agent, variant, original):
        template = (ROOT / 'integrations' / TEMPLATES[agent]).read_bytes()
        if agent in ('claude', 'codex', 'grok', 'cursor', 'copilot'):
            data = json.loads(original)
            if variant == 'old':
                # Fixtures put the two legacy groups after the independent hook.
                event = next(iter(data['hooks']))
                data['hooks'][event] = data['hooks'][event][:1]
            baseline = (json.dumps(data, indent=2) + '\n').encode() if variant == 'old' else original
            expected = json.loads(json.dumps(data))
            desired = json.loads(template)
            for event, entries in desired['hooks'].items():
                expected.setdefault('hooks', {}).setdefault(event, []).extend(entries)
            expected.update({k: v for k, v in desired.items() if k == 'version'})
            return (json.dumps(expected, ensure_ascii=False, indent=2) + '\n').encode(), baseline
        if agent == 'kimi':
            baseline = original
            if variant == 'old':
                baseline = original[:original.index(b'[[hooks]]\nevent = "Stop"\ncommand = "herdcat')]
            expected = baseline + b'# >>> herdcat >>>\n' + template.rstrip() + b'\n# <<< herdcat <<<\n'
            return expected, baseline
        if agent == 'pi':
            return template, None
        entry = json.dumps(str(self.home / '.config/opencode/herdcat-plugin')).encode()
        if variant == 'blank':
            return b'{\n  "plugin": [' + entry + b']\n}\n', original
        if variant == 'other':
            return original.replace(b'"/fake/audit-plugin",]', b'"/fake/audit-plugin",' + entry + b']'), original
        # A single already correct plugin path needs only a bridge update.
        baseline = original.replace(b'"@HERDCAT_PLUGIN@"', b'')
        # Text removal retains the space preceding the removed entry.
        baseline = original.replace(b' ' + entry + b',', b' ')
        return original, baseline

    def test_all_fixtures(self):
        for agent in TARGETS:
            for variant in ('blank', 'other', 'old'):
                with self.subTest(agent=agent, variant=variant):
                    # Every fixture gets its own HOME and state store.
                    child = SetupTests()
                    child.setUp()
                    try:
                        path, original = child.fixture(agent, variant)
                        expected, removed = child.expected(agent, variant, original)
                        before = snapshot(child.home)
                        child.run_setup(agent, '--dry-run')
                        self.assertEqual(snapshot(child.home), before)
                        child.run_setup(agent, '--yes')
                        self.assertEqual(path.read_bytes(), expected)
                        if agent in ('claude', 'codex', 'grok', 'cursor', 'copilot'):
                            json.loads(path.read_text())
                        elif agent == 'kimi':
                            import tomllib
                            tomllib.loads(path.read_text())
                        elif agent == 'opencode':
                            setup.Jsonc(path.read_text())
                            self.assertEqual((path.parent / 'herdcat-plugin/index.js').read_bytes(),
                                             (ROOT / 'integrations/opencode/index.js').read_bytes())
                        self.assertEqual(stat.S_IMODE(path.stat().st_mode),
                                         0o600 if original is None or agent == 'kimi' else 0o640)
                        backups = list((child.home / '.local/state/herdcat/backups').rglob('*'))
                        if original is not None and original != expected:
                            copies = [p for p in backups if p.is_file() and p.read_bytes() == original]
                            self.assertTrue(copies)
                            self.assertEqual(stat.S_IMODE(copies[0].stat().st_mode),
                                             0o600 if agent == 'kimi' else 0o640)
                        current = snapshot(child.home)
                        child.run_setup(agent, '--yes')
                        self.assertEqual(snapshot(child.home), current)
                        self.assertIn(b'connected', child.run_setup(agent, '--status'))
                        child.run_setup('--remove', agent, '--dry-run')
                        self.assertEqual(snapshot(child.home), current)
                        child.run_setup('--remove', agent, '--yes')
                        if removed is None:
                            self.assertFalse(path.exists())
                        else:
                            self.assertEqual(path.read_bytes(), removed)
                        current = snapshot(child.home)
                        child.run_setup('--remove', agent, '--yes')
                        self.assertEqual(snapshot(child.home), current)
                    finally:
                        child.tearDown()

    def test_terminal_fixtures_roundtrip(self):
        for terminal in ('tmux', 'kitty'):
            for variant in ('blank', 'other', 'old'):
                with self.subTest(terminal=terminal, variant=variant):
                    child = SetupTests()
                    child.setUp()
                    try:
                        path = child.home / ('.config/tmux/tmux.conf' if terminal == 'tmux'
                                             else '.config/kitty/kitty.conf')
                        original = (FIXTURES / terminal / (variant + '.conf')).read_bytes()
                        child.write(path, original)
                        baseline = original.split(b'# >>> herdcat >>>')[0]
                        watcher = path.parent / 'herdcat_watcher.py'
                        if terminal == 'kitty' and variant == 'old':
                            child.write(watcher, b'# old herdcat watcher\ndef send_pane(pid, split): pass\n')
                        before = snapshot(child.home)
                        child.run_setup(terminal, '--dry-run')
                        self.assertEqual(snapshot(child.home), before)
                        child.run_setup(terminal, '--yes')
                        installed = path.read_bytes()
                        self.assertEqual(installed.count(b'# >>> herdcat >>>'), 1)
                        self.assertTrue(installed.startswith(baseline))
                        self.assertIn(b'source-file' if terminal == 'tmux' else b'watcher herdcat_watcher.py', installed)
                        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o640)
                        if terminal == 'kitty':
                            self.assertEqual(watcher.read_bytes(),
                                             (ROOT / 'integrations/kitty/herdcat_watcher.py').read_bytes())
                            self.assertIn(b'allow_remote_control password', installed)
                        backups = child.home / '.local/state/herdcat/backups'
                        self.assertTrue(any(p.is_file() and p.read_bytes() == original
                                            for p in backups.rglob('*')))
                        current = snapshot(child.home)
                        child.run_setup(terminal, '--yes')
                        self.assertEqual(snapshot(child.home), current)
                        self.assertIn(b'connected', child.run_setup(terminal, '--status'))
                        child.run_setup(terminal, '--remove', '--dry-run')
                        self.assertEqual(snapshot(child.home), current)
                        child.run_setup(terminal, '--remove', '--yes')
                        self.assertEqual(path.read_bytes(), baseline)
                        if terminal == 'kitty':
                            self.assertFalse(watcher.exists())
                        current = snapshot(child.home)
                        child.run_setup(terminal, '--remove', '--yes')
                        self.assertEqual(snapshot(child.home), current)
                    finally:
                        child.tearDown()

    def test_tmux_legacy_path_and_new_configs(self):
        legacy = self.home / '.tmux.conf'
        self.write(legacy, b'set -g status off\n')
        self.run_setup('tmux', '--yes')
        self.assertIn(b'source-file', legacy.read_bytes())
        self.assertFalse((self.home / '.config/tmux/tmux.conf').exists())
        self.run_setup('tmux', '--remove', '--yes')
        self.assertEqual(legacy.read_bytes(), b'set -g status off\n')
        legacy.unlink()
        self.run_setup('tmux', 'kitty', '--yes')
        self.run_setup('tmux', 'kitty', '--remove', '--yes')
        self.assertFalse((self.home / '.config/tmux/tmux.conf').exists())
        self.assertFalse((self.home / '.config/kitty/kitty.conf').exists())
        self.assertFalse((self.home / '.config/kitty/herdcat_watcher.py').exists())

    def test_invalid_json_and_schema_are_untouched(self):
        for content in (b'{"key":"sk-FAKE-DO-NOT-USE",', b'{"hooks":[]}',
                        b'{"hooks":{"Stop":{}}}', b'{"hooks":null}',
                        b'{"hooks":{},"hooks":{}}', b'{"number":NaN}'):
            path = self.home / TARGETS['claude']
            self.write(path, content)
            before = snapshot(self.home)
            self.run_setup('claude', '--yes', code=1)
            self.assertEqual(snapshot(self.home), before)

    def test_corrupt_jsonc_does_not_copy_plugin(self):
        path = self.home / TARGETS['opencode']
        for content in (b'{"api_key":"sk-FAKE-DO-NOT-USE",', b'{"plugin":{}}',
                        b'{/* unclosed', b'{"plugin":[],"plugin":[]}'):
            self.write(path, content)
            before = snapshot(self.home)
            self.run_setup('opencode', '--yes', code=1)
            self.assertEqual(snapshot(self.home), before)

    def test_symlink_preserved(self):
        target = self.home / 'dotfiles/settings.json'
        original = b'{ "api_key": "sk-FAKE-DO-NOT-USE" }'
        self.write(target, original, 0o600)
        path = self.home / TARGETS['claude']
        path.parent.mkdir()
        path.symlink_to(target)
        self.assertIn(b'symlink target', self.run_setup('claude', '--yes'))
        self.assertTrue(path.is_symlink())
        self.assertIn('hooks', json.loads(target.read_text()))
        self.run_setup('claude', '--remove', '--yes')
        self.assertTrue(path.is_symlink())
        self.assertEqual(target.read_bytes(), original)

    def test_unrelated_edits_survive_remove(self):
        path, _ = self.fixture('claude', 'other')
        self.run_setup('claude', '--yes')
        data = json.loads(path.read_text())
        data['new_setting'] = 'keep this'
        data['hooks']['Stop'][0]['hooks'].append({'type': 'command', 'command': 'echo new'})
        self.write(path, json.dumps(data).encode())
        self.run_setup('claude', '--remove', '--yes')
        removed = json.loads(path.read_text())
        self.assertEqual(removed['new_setting'], 'keep this')
        self.assertEqual(len(removed['hooks']['Stop'][0]['hooks']), 2)
        self.assertNotIn('herdcat --hook', path.read_text())

    def test_mixed_nested_hooks(self):
        path = self.home / TARGETS['codex']
        self.write(path, b'{"hooks":{"Stop":[{"matcher":"*","hooks":['
                   b'{"command":"herdcat --hook codex --old"},'
                   b'{"command":"echo audit"}]}]}}')
        self.run_setup('codex', '--yes')
        hooks = json.loads(path.read_text())['hooks']['Stop']
        self.assertEqual(hooks[0], {'matcher': '*', 'hooks': [{'command': 'echo audit'}]})
        self.run_setup('codex', '--remove', '--yes')
        self.assertEqual(json.loads(path.read_text())['hooks']['Stop'], hooks[:1])

    def test_kimi_no_final_newline_and_external_edits(self):
        path = self.home / TARGETS['kimi']
        original = b'[providers.fake]\napi_key = "sk-FAKE-DO-NOT-USE"'
        self.write(path, original, 0o600)
        self.run_setup('kimi', '--yes')
        self.assertTrue(path.read_bytes().startswith(original))
        self.run_setup('kimi', '--remove', '--yes')
        self.assertEqual(path.read_bytes(), original)
        self.write(path, original + b'\n')
        self.run_setup('kimi', '--yes')
        with path.open('ab') as stream:
            stream.write(b'\n[extra]\nvalue = "keep"\n')
        self.run_setup('kimi', '--remove', '--yes')
        self.assertEqual(path.read_bytes(), original + b'\n\n[extra]\nvalue = "keep"\n')

    def test_ambiguous_kimi_markers(self):
        path = self.home / TARGETS['kimi']
        for content in (b'# >>> herdcat >>>\n', b'# <<< herdcat <<<\n', b'hooks = []\n'):
            self.write(path, content)
            before = snapshot(self.home)
            self.run_setup('kimi', '--yes', code=1)
            self.assertEqual(snapshot(self.home), before)

    def test_opencode_plural_and_comments(self):
        path = self.home / TARGETS['opencode']
        original = b'{/*top*/"plugins":["other" /*tail*/],"url":"https://example.invalid/a//b",}'
        self.write(path, original)
        self.run_setup('opencode', '--yes')
        after = path.read_bytes()
        self.assertIn(b'/*top*/', after)
        self.assertIn(b'/*tail*/', after)
        self.assertEqual(setup.Jsonc(after.decode()).root.value['plugins'][0], 'other')
        self.run_setup('opencode', '--yes')
        self.assertEqual(path.read_bytes(), after)
        self.run_setup('opencode', '--remove', '--yes')
        self.assertEqual(path.read_bytes(), original)

    def test_opencode_legacy_bridge_location(self):
        # Earlier manual instructions put the bridge under the data directory.
        # Its entry must be replaced, not joined by a second one.
        legacy = self.home / '.local/share/herdcat/opencode'
        bridge = ROOT / 'integrations/opencode/index.js'
        self.write(legacy / 'index.js', bridge.read_bytes())
        foreign = self.home / 'plugins/other'
        self.write(foreign / 'index.js', b'export default {};\n')
        path = self.home / TARGETS['opencode']
        original = ('{"plugin": ["superpowers", "%s", "%s"]}\n' % (legacy, foreign)).encode()
        self.write(path, original)
        self.assertIn(b'outdated', self.run_setup('opencode', '--status'))
        self.run_setup('opencode', '--yes')
        plugins = setup.Jsonc(path.read_text()).root.value['plugin']
        self.assertEqual(plugins.count(str(legacy)), 0)
        self.assertIn('superpowers', plugins)
        self.assertIn(str(foreign), plugins)
        self.assertEqual(len(plugins), 3)
        # The old files are the user's; they are left where they were.
        self.assertTrue((legacy / 'index.js').exists())
        self.assertIn(b'connected', self.run_setup('opencode', '--status'))
        self.assertNotIn(b'outdated', self.run_setup('opencode', '--status'))

    def test_opencode_json_fallback(self):
        path = (self.home / TARGETS['opencode']).with_suffix('.json')
        original = b'{ "plugin": [] }\n'
        self.write(path, original)
        self.run_setup('opencode', '--yes')
        json.loads(path.read_text())
        self.assertFalse(path.with_suffix('.jsonc').exists())
        self.run_setup('opencode', '--remove', '--yes')
        self.assertEqual(path.read_bytes(), original)

    def test_jsonc_remove_duplicates_without_receipts(self):
        plugin = self.home / '.config/opencode/herdcat-plugin'
        owned = json.dumps(str(plugin))
        path = self.home / TARGETS['opencode']
        for entries in ([owned, owned], ['"keep"', owned, owned],
                        [owned, '"keep"', owned], [owned, owned, '"keep"'],
                        ['"keep"', owned, '"last"', owned]):
            for trailing in ('', ','):
                with self.subTest(entries=entries, trailing=trailing):
                    text = '{/* preserve */"plugin":[' + ','.join(entries) + trailing + ']}'
                    self.write(path, text.encode())
                    self.run_setup('opencode', '--remove', '--yes')
                    result = setup.Jsonc(path.read_text()).root.value
                    self.assertEqual(result['plugin'], [json.loads(e) for e in entries if e != owned])
                    self.assertIn(b'/* preserve */', path.read_bytes())

    def test_new_files_are_removed_and_updated_templates_are_reversible(self):
        prefix = self.home / 'prefix'
        script = prefix / 'bin/herdcat-setup'
        script.parent.mkdir(parents=True)
        shutil.copyfile(SCRIPT, script)
        templates = prefix / 'share/herdcat/integrations'
        shutil.copytree(ROOT / 'integrations', templates)
        def run(*args):
            result = subprocess.run([sys.executable, str(script), *args], env=self.env,
                                    input=b'', capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout
        path = self.home / TARGETS['codex']
        original = b'{"api_key":"sk-FAKE-DO-NOT-USE"}'
        self.write(path, original)
        run('codex', '--yes')
        source = templates / 'hooks/codex.hooks.json'
        data = json.loads(source.read_text())
        del data['hooks']['PreToolUse']
        data['hooks']['Stop'][0]['hooks'][0]['timeout'] = 2
        source.write_text(json.dumps(data))
        self.assertIn(b'outdated', run('codex', '--status'))
        run('codex', '--yes')
        self.assertNotIn('PreToolUse', json.loads(path.read_text())['hooks'])
        self.assertIn(b'connected', run('codex', '--status'))
        run('codex', '--remove', '--yes')
        self.assertEqual(path.read_bytes(), original)
        path.unlink()
        run('codex', '--yes')
        run('codex', '--remove', '--yes')
        self.assertFalse(path.exists())

    def test_bad_state_receipt_is_rejected(self):
        path, _ = self.fixture('claude', 'blank')
        self.run_setup('claude', '--yes')
        receipt = next((self.home / '.local/state/herdcat/setup').glob('claude-*.json'))
        record = json.loads(receipt.read_text())
        record['restore'] = str(self.home / 'unrelated.json')
        self.write(self.home / 'unrelated.json', b'{}')
        receipt.write_text(json.dumps(record))
        before = snapshot(self.home)
        self.run_setup('claude', '--remove', '--yes', code=1)
        self.assertEqual(snapshot(self.home), before)

    def test_foreign_bridge_is_protected(self):
        path = self.home / TARGETS['pi']
        self.write(path, b'export default function unrelated() {}\n')
        before = snapshot(self.home)
        self.run_setup('pi', '--yes', code=1)
        self.assertEqual(snapshot(self.home), before)

    def test_noninteractive_needs_yes(self):
        path, _ = self.fixture('claude', 'blank')
        before = snapshot(self.home)
        self.assertIn(b'--yes', self.run_setup('claude', code=1))
        self.assertEqual(snapshot(self.home), before)
        self.assertFalse('hooks' in json.loads(path.read_text()))

    def test_interactive_confirmation_defaults_to_no(self):
        self.fixture('claude', 'blank')
        before = snapshot(self.home)
        class Terminal(io.StringIO):
            def isatty(self):
                return True
        output = Terminal()
        with patch.dict(os.environ, self.env, clear=True), \
                patch('sys.stdin', Terminal()), patch('sys.stdout', output), \
                patch('builtins.input', return_value=''):
            self.assertEqual(setup.main(['claude']), 0)
        self.assertEqual(snapshot(self.home), before)
        self.assertIn('Cancelled', output.getvalue())

    def test_detection_and_locale(self):
        output = self.run_setup('--status')
        self.assertEqual(output.count(b'not installed'), 10)
        (self.home / '.codex').mkdir()
        output = self.run_setup('--status')
        self.assertIn(b'codex: not connected', output)
        (self.bin / 'claude').write_text('#!/bin/sh\nexit 99\n')
        (self.bin / 'claude').chmod(0o755)
        self.assertIn(b'claude: not connected', self.run_setup('--status'))
        self.assertIn('未接入'.encode(), self.run_setup('--status', env={**self.env, 'LC_MESSAGES': 'zh_CN.UTF-8'}))
        before = snapshot(self.home)
        self.run_setup('--dry-run')
        self.assertEqual(snapshot(self.home), before)

    def test_custom_homes(self):
        env = {**self.env, 'CODEX_HOME': str(self.home / 'custom-codex'),
               'KIMI_CODE_HOME': str(self.home / 'custom-kimi'),
               'PI_CODING_AGENT_DIR': str(self.home / 'custom-pi')}
        self.run_setup('codex', 'kimi', 'pi', '--yes', env=env)
        for relative in ('custom-codex/hooks.json', 'custom-kimi/config.toml', 'custom-pi/extensions/herdcat.ts'):
            self.assertTrue((self.home / relative).is_file())
        self.assertFalse((self.home / '.codex').exists())

    def test_update_is_reported_and_old_hooks_removed(self):
        path, _ = self.fixture('claude', 'old')
        self.assertIn(b'outdated', self.run_setup('claude', '--status'))
        self.run_setup('claude', '--yes')
        self.assertNotIn(b'bongocat --hook', path.read_bytes())
        self.assertNotIn(b'--old', path.read_bytes())
        self.assertIn(b'already current', self.run_setup('claude', '--dry-run'))

    def test_installed_layout_templates(self):
        prefix = self.home / 'prefix'
        script = prefix / 'bin/herdcat-setup'
        script.parent.mkdir(parents=True)
        shutil.copyfile(SCRIPT, script)
        shutil.copytree(ROOT / 'integrations', prefix / 'share/herdcat/integrations')
        result = subprocess.run([sys.executable, str(script), 'codex', '--yes'],
                                env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'/hooks', result.stdout)

    def test_c_entry_forwards_and_reports_missing_script(self):
        binary = ROOT / 'build/herdcat'
        if not binary.exists():
            self.skipTest('build/herdcat is unavailable')
        helper = self.bin / 'herdcat-setup'
        helper.write_text('#!' + sys.executable + '\nimport json,sys\n'
                          'print(json.dumps(sys.argv[1:]))\nsys.exit(7)\n')
        helper.chmod(0o755)
        result = subprocess.run([str(binary), 'setup', 'claude', '--dry-run'],
                                env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['claude', '--dry-run'])
        helper.unlink()
        result = subprocess.run([str(binary), 'setup', '--status'], env=self.env,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'Install the setup script and Python 3', result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
