#!/usr/bin/env python3
"""Capture real hooks in a private tmux server, never forward to the desktop."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import signal
import shutil
import subprocess
import sys
import tempfile
import time

from agent_recording import ROOT, Redactor, check_private, checked_json

# The shim has no imports from the worktree and never executes real herdcat.
SHIM = '''#!/usr/bin/env python3
import fcntl, json, os, sys, time
raw = sys.stdin.read()
entry = {'ms': time.monotonic_ns() // 1000000, 'argv': ['herdcat', *sys.argv[1:]], 'stdin': raw}
with open(os.environ['HERDCAT_RECORD_LOG'], 'a') as log:
    fcntl.flock(log, fcntl.LOCK_EX)
    log.write(json.dumps(entry) + '\\n')
    log.flush()
response = os.environ.get('HERDCAT_RECORD_STDOUT', '{}\\n' if 'copilot' in sys.argv else '')
if response:
    sys.stdout.write(response)
'''


def load_recipe(agent):
    if not re.fullmatch(r'[a-z][a-z0-9_-]*', agent):
        raise ValueError('invalid agent name')
    return json.loads((ROOT / 'scripts/agent_recipes' / f'{agent}.json').read_text())


def installed_version(recipe):
    with tempfile.TemporaryDirectory(prefix='hc-version-', dir='/tmp') as tmp:
        root = Path(tmp)
        (root / '.git').mkdir()
        env = os.environ.copy()
        env.update(HOME=tmp, XDG_RUNTIME_DIR=tmp, CLAUDE_CONFIG_DIR=tmp,
                   CODEX_HOME=tmp, COPILOT_HOME=tmp, PI_CODING_AGENT_DIR=tmp,
                   XDG_CONFIG_HOME=tmp, XDG_CACHE_HOME=tmp, XDG_DATA_HOME=tmp)
        for key in ['WAYLAND_DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'TMUX', 'TMUX_PANE']:
            env.pop(key, None)
        result = subprocess.run(recipe.get('version_command', [recipe['agent'], '--version']),
                                env=env, cwd=root, capture_output=True, text=True,
                                timeout=20, check=True)
    output = result.stdout.strip()
    match = re.search(recipe.get('version_regex', r'\b\d+\.\d+\.\d+\b'), output)
    if not match:
        raise ValueError('version command did not return a version')
    return match.group(), output


def isolated_environment(recipe, root, original_home, version=None):
    """Copy login material, never modify the source. All caches stay in /tmp."""
    env = os.environ.copy()
    for name in list(env):
        if name in {'WAYLAND_DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'DISPLAY',
                    'TMUX', 'TMUX_PANE', 'HERDCAT_HOOK_DEBUG', 'CLAUDE_PID',
                    'CODEX_THREAD_ID', 'CODEX_INTERNAL_ORIGINATOR_OVERRIDE'}:
            env.pop(name, None)
    home = root / 'home'
    home.mkdir(mode=0o700)
    env.update(HOME=str(home), XDG_RUNTIME_DIR=str(root),
               XDG_CONFIG_HOME=str(home / '.config'), XDG_CACHE_HOME=str(home / '.cache'),
               XDG_DATA_HOME=str(home / '.local/share'), TMUX_TMPDIR=str(root),
               CLAUDE_CODE_DISABLE_AUTOUPDATER='1', DISABLE_AUTOUPDATER='1',
               PI_OFFLINE='1', PI_TELEMETRY='0')
    agent = recipe['agent']
    config = recipe['config']
    variable, relative, files = config['env'], config['directory'], config['files']
    source = Path(os.environ.get(variable, str(original_home / relative))) if variable else original_home / relative
    target = home / relative
    target.mkdir(parents=True, mode=0o700)
    if variable:
        env[variable] = str(target)
    for name in files:
        src, dst = source / name, target / name
        if src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            shutil.copyfile(src, dst)
            dst.chmod(0o600)
    for name in config.get('home_files', []):
        src, dst = original_home / name, home / name
        if src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            shutil.copyfile(src, dst)
            dst.chmod(0o600)
    if agent == 'claude':
        # The config directory has a separate onboarding/trust state file.
        for src in [source / '.claude.json', original_home / '.claude.json']:
            if src.exists():
                shutil.copyfile(src, home / '.claude.json')
                (home / '.claude.json').chmod(0o600)
                break
        for state_path in [home / '.claude.json', target / '.claude.json']:
            if state_path.exists():
                state = json.loads(state_path.read_text())
                state['projects'] = {str(root / 'project'): {'hasTrustDialogAccepted': True}}
                state['hasCompletedOnboarding'] = True
                state_path.write_text(json.dumps(state))
        settings = target / 'settings.json'
        data = json.loads(settings.read_text())
        data.setdefault('permissions', {}).setdefault('ask', []).append('Bash')
        data['enabledPlugins'] = {}
        data['remoteControlAtStartup'] = False
        settings.write_text(json.dumps(data))
    if agent == 'codex':
        config = target / 'config.toml'
        if config.exists():
            # Preserve provider credentials/model settings but do not load plugins,
            # project instructions or user marketplace integrations in the probe.
            section, kept = '', []
            for line in config.read_text().splitlines():
                if line.lstrip().startswith('['):
                    section = line.lstrip().split(']', 1)[0].strip('[').split('.', 1)[0]
                if section in {'projects', 'plugins', 'marketplaces', 'desktop', 'hooks'}:
                    continue
                kept.append(line)
            config.write_text('\n'.join(kept) + '\n')
    if agent == 'copilot':
        # Use the already logged-in GitHub CLI credential without displaying it.
        # Copilot's encrypted config may not decrypt with a different HOME.
        if not any(env.get(k) for k in ['COPILOT_GITHUB_TOKEN', 'GH_TOKEN', 'GITHUB_TOKEN']):
            token = subprocess.run(['gh', 'auth', 'token'], capture_output=True,
                                   text=True, timeout=10)
            if token.returncode == 0:
                env['COPILOT_GITHUB_TOKEN'] = token.stdout.strip()
    if agent == 'pi':
        settings = target / 'settings.json'
        if settings.exists():
            data = json.loads(settings.read_text())
            if version:
                data['lastChangelogVersion'] = version
            for key in ['packages', 'extensions', 'skills', 'promptTemplates', 'themes', 'mcpServers']:
                data.pop(key, None)
            settings.write_text(json.dumps(data))
    return env


def verify_interception(recipe, home):
    """Fail closed on absolute commands, missing bridges or unvetted hooks."""
    agent = recipe['agent']
    spec = recipe['config']
    source = Path(home) / spec['directory'] / spec['hook_file']
    if spec['hook_format'] == 'bridge':
        if not re.search(spec['bridge_pattern'], source.read_text()):
            raise ValueError('bridge does not invoke a PATH-resolved herdcat')
        return
    if spec['hook_format'] == 'toml':
        import tomllib
        config = tomllib.loads(source.read_text())
    else:
        config = json.loads(source.read_text())
    commands = []

    def walk(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key in {'command', 'bash'}:
                    commands.append(item)
                else:
                    walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)
    walk(config.get(spec.get('hooks_key', 'hooks'), {}))
    if not commands or any(not re.fullmatch(
            r'herdcat --hook ' + agent + r'( --event [A-Za-z_]+)?( >/?dev/null)?( 2>&1| 2>/dev/null)?( \|\| true)?', c)
            for c in commands):
        raise ValueError('hooks contain missing, absolute or non-herdcat commands; cannot intercept safely')


class Driver:
    def __init__(self, root, env, command, cwd, timeout):
        self.root, self.env, self.timeout = root, env, timeout
        self.server = f'herdcat-record-{os.getpid()}'
        self.base = ['tmux', '-L', self.server, '-f', '/dev/null']
        self.run('new-session', '-d', '-s', 'probe', '-x', '120', '-y', '45',
                 '-c', str(cwd), shlex.join(command))
        self.run('set-option', '-g', 'remain-on-exit', 'on')

    def run(self, *args, check=True):
        return subprocess.run(self.base + list(args), env=self.env,
                              capture_output=True, text=True, timeout=10, check=check)

    def screen(self):
        return self.run('capture-pane', '-p', '-t', 'probe:0.0', check=False).stdout

    def keys(self, keys):
        for key in keys:
            if key.startswith('text:'):
                self.run('send-keys', '-t', 'probe:0.0', '-l', key[5:])
            elif key.startswith('/'):
                self.run('send-keys', '-t', 'probe:0.0', '-l', key)
            else:
                self.run('send-keys', '-t', 'probe:0.0', key)
            if key.startswith(('text:', '/')):
                time.sleep(0.2)

    def wait(self, predicate, label):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.1)
        raise TimeoutError(f'timeout waiting for {label}')

    def close(self):
        # Match the private environment tag, then guard against PID reuse.
        # This also catches daemonized descendants reparented away from tmux.
        owned = {}
        tag = ('HERDCAT_RECORD_LOG=' + self.env['HERDCAT_RECORD_LOG']).encode()
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit() or int(proc.name) == os.getpid():
                continue
            try:
                if tag in (proc / 'environ').read_bytes().split(b'\0'):
                    owned[int(proc.name)] = (proc / 'stat').read_text().rsplit(')', 1)[1].split()[19]
            except (OSError, IndexError):
                continue
        self.run('kill-server', check=False)
        def alive(pid, started):
            try:
                fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
                return fields[19] == started and fields[0] != 'Z'
            except (OSError, IndexError):
                return False
        for sig in [signal.SIGTERM, signal.SIGKILL]:
            for pid, started in owned.items():
                if alive(pid, started):
                    try:
                        os.kill(pid, sig)
                    except ProcessLookupError:
                        pass
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and any(alive(p, t) for p, t in owned.items()):
                time.sleep(0.05)



def capture_scope(hooks, marks):
    """Cleanup is outside the authored scenario, never a format-change signal."""
    if not marks:
        return hooks
    cutoff = max(mark['ms'] for mark in marks)
    return [hook for hook in hooks if hook['ms'] <= cutoff]


def record(recipe, scenario, output, isolate=True):
    spec = recipe['scenarios'][scenario]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    if spec.get('skip'):
        checked_json(output / 'skipped.json', {'scenario': scenario, 'reason': spec['skip']},
                     str(Path.home()), pwd.getpwuid(os.getuid()).pw_name)
        return 'skipped'
    if any(output.iterdir()):
        raise ValueError('output scenario directory must be empty')
    version, version_output = installed_version(recipe)
    original_home = Path.home()
    username = pwd.getpwuid(os.getuid()).pw_name
    redactor = Redactor(str(original_home), username)
    timeout = recipe.get('timeout', 45)
    driver, marks = None, []
    with tempfile.TemporaryDirectory(prefix='hc-rec-', dir='/tmp') as temporary:
        root = Path(temporary)
        root.chmod(0o700)
        project, shim = root / 'project', root / 'bin'
        project.mkdir()
        (project / '.git').mkdir()
        shim.mkdir()
        (shim / 'herdcat').write_text(SHIM)
        (shim / 'herdcat').chmod(0o700)
        log = root / 'hooks.jsonl'
        env = isolated_environment(recipe, root, original_home, version) if isolate else os.environ.copy()
        for name in ['WAYLAND_DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'DISPLAY', 'TMUX', 'TMUX_PANE']:
            env.pop(name, None)
        env.update(PATH=str(shim) + os.pathsep + os.environ['PATH'],
                   HERDCAT_RECORD_LOG=str(log), HERDCAT_RECORD_STDOUT=recipe.get('shim_stdout', ''), XDG_RUNTIME_DIR=str(root), TMUX_TMPDIR=str(root))
        if isolate:
            verify_interception(recipe, env['HOME'])
        command = recipe['command'] + spec.get('args', [])

        def has_event(event, after=0):
            return any(item['event'] == event for item in entries()[after:])

        def mark(state, label):
            item = {'ms': time.monotonic_ns() // 1000000, 'state': state, 'label': label}
            marks.append(item)
            # Markers use the same lock and monotonic time base as the shim.
            with log.open('a') as stream:
                fcntl.flock(stream, fcntl.LOCK_EX)
                stream.write(json.dumps({'milestone': item}) + '\n')

        def hooks_only():
            if not log.exists():
                return []
            with log.open() as stream:
                fcntl.flock(stream, fcntl.LOCK_SH)
                text = stream.read()
            return [{**json.loads(line), 'source_line': index}
                    for index, line in enumerate(text.splitlines(), 1)
                    if '"milestone"' not in line]

        # entries ignores authored milestones; hook data always comes from stdin.
        def entries():
            result = []
            for item in hooks_only():
                item['payload'] = json.loads(item['stdin'])
                args = item['argv']
                item['event'] = args[args.index('--event') + 1] if '--event' in args else next((item['payload'][key] for key in recipe.get('event_fields', ['hook_event_name', 'hookEventName', 'hookName']) if key in item['payload']), '')
                result.append(item)
            return result

        def save_screen(name):
            screen = driver.screen()
            screen = redactor.clean(screen)
            check_private(screen, str(original_home), username)
            (output / name).write_text(screen)

        try:
            driver = Driver(root, env, command, project, timeout)
            # Answer only declared local onboarding dialogs; never login prompts.
            handled_dialogs = set()
            startup_deadline = time.monotonic() + timeout
            while time.monotonic() < startup_deadline:
                screen = driver.screen()
                dialog = next((d for d in recipe.get('startup', []) if re.search(d['screen'], screen)), None)
                if dialog and dialog['screen'] not in handled_dialogs:
                    handled_dialogs.add(dialog['screen'])
                    driver.keys(dialog['keys'])
                    time.sleep(0.8)
                elif re.search(recipe['ready'], screen) and (recipe.get('lazy_start') or has_event(recipe['start_event'])):
                    break
                else:
                    time.sleep(0.2)
            if not recipe.get('lazy_start'):
                driver.wait(lambda: has_event(recipe['start_event']), 'first intercepted session hook')
            driver.wait(lambda: re.search(recipe['ready'], driver.screen()), 'ready screen')
            mark('absent' if recipe.get('lazy_start') else 'idle', 'ready before first submission')
            before = len(entries())
            if scenario != 'exit' or recipe.get('lazy_start'):
                prompt = spec.get('prompt', 'Reply with the single word ok')
                checked_json(output / 'calls.json', {'probe_turns_submitted': 1}, str(original_home), username)
                driver.keys(['text:' + prompt, 'Enter'])
                driver.wait(lambda: has_event(recipe.get('submit_event', 'UserPromptSubmit'), before), 'submission hook')
                if recipe.get('lazy_start'):
                    driver.wait(lambda: has_event(recipe['start_event']), 'first intercepted session hook')
                mark('working', 'prompt submitted')
                if scenario in {'approve', 'deny'}:
                    driver.wait(lambda: re.search(recipe['approval_screen'], driver.screen()), 'permission dialog')
                    mark('waiting', 'permission dialog visible')
                    save_screen('permission.txt')
                    driver.keys(recipe[scenario + '_keys'])
                if scenario == 'cancel':
                    driver.wait(lambda: re.search(recipe['working_screen'], driver.screen()), 'reply in progress')
                    driver.keys(recipe['cancel_keys'])
                    driver.wait(lambda: has_event(recipe['cancel_event'], before), 'cancellation hook')
                    mark('idle', 'cancelled')
                else:
                    end_event = spec.get('end_event', recipe['end_event'])
                    if end_event:
                        driver.wait(lambda: has_event(end_event, before), 'answer completion hook')
                    driver.wait(lambda: re.search(spec.get('end_screen', recipe['answer']), driver.screen()), 'answer screen')
                    mark(spec.get('end_state', 'done'), 'answer ended or refused')
                save_screen('completed.txt')
            if scenario == 'exit':
                driver.keys(recipe['exit_keys'])
                driver.wait(lambda: driver.run('display-message', '-p', '-t', 'probe:0.0', '#{pane_dead}', check=False).stdout.strip() == '1', 'agent process exit')
                time.sleep(0.2)  # Let an already spawned final shim flush.
                mark('absent', 'session exited')
            save_screen('last-screen.txt')
            result = 'recorded'
        except (TimeoutError, ValueError, subprocess.SubprocessError, OSError) as exc:
            if driver:
                save_screen('failure-screen.txt')
            checked_json(output / 'failure.json', {'error': redactor.clean(str(exc)),
                         'agent': recipe['agent'], 'scenario': scenario}, str(original_home), username)
            result = 'needs-attention'
        finally:
            if driver:
                driver.close()
        raw = log.read_bytes() if log.exists() else b''
        captured = capture_scope(entries(), marks) if result == 'recorded' else entries()
        if not captured:
            checked_json(output / 'failure.json', {'error': 'no intercepted hooks; empty recording rejected'}, str(original_home), username)
            return 'needs-attention'
        # Expectations below are a script-author contract, never replay output.
        baseline = min([item['ms'] for item in captured] + [m['ms'] for m in marks])
        lines = ['# elapsed_ms\tpayload\t--event\tstate\ttitle\tsubagent']
        state, title, steps = 'absent', '-', []
        timeline = [(item['ms'], index, item) for index, item in enumerate(captured, 1)]
        timeline += [(m['ms'], 0, m) for m in marks]
        events = recipe.get('events', {})
        default_events = {'SessionStart': 'idle', 'UserPromptSubmit': 'working',
                          'PreToolUse': 'working', 'PostToolUse': 'working',
                          'PostToolUseFailure': 'working', 'PermissionRequest': 'waiting',
                          'Stop': 'done', 'StopFailure': 'error', 'Interrupt': 'idle',
                          'SessionEnd': 'absent'}
        events = events or default_events
        for ms, index, item in sorted(timeline, key=lambda x: (x[0], x[1] == 0)):
            if not index:
                expected = item['state']
                lines.append(f'{ms-baseline}\t-\t-\t{expected}\t{title if expected != "absent" else "-"}\t0')
                continue
            clean = redactor.clean(item['payload'])
            filename = f'{index:03}.json'
            checked_json(output / filename, clean, str(original_home), username)
            event = item['event']
            expected = events.get(event, 'preserve')
            if event in {'SessionStart', 'sessionStart', 'session_start'} and state != 'absent':
                expected = 'preserve'
            if event in {'Notification', 'notification'}:
                expected = recipe.get('notifications', {}).get(clean.get('notification_type'), 'preserve')
            if event in {'agentStop', 'agent_end'}:
                expected = recipe['stop_reasons'].get(clean.get('stopReason'), 'preserve')
            if event == 'Stop' and clean.get('stop_hook_active') is True:
                expected = 'preserve'
            if expected != 'preserve':
                state = expected
            if event == recipe.get('submit_event', 'UserPromptSubmit') and clean.get('prompt') and title == '-':
                title = clean['prompt']
            if state == 'absent':
                title = '-'
            args = item['argv']
            explicit = args[args.index('--event') + 1] if '--event' in args else '-'
            lines.append(f'{ms-baseline}\t{filename}\t{explicit}\t{state}\t{title}\t0')
            steps.append({'payload': filename, 'source_line': item['source_line'],
                          'argv': redactor.clean(args), 'received_monotonic_ms': ms})
        expectations = '\n'.join(lines) + '\n'
        check_private(expectations, str(original_home), username)
        (output / 'expect.tsv').write_text(expectations)
        checked_json(output / 'provenance.json', {
            'agent': recipe['agent'], 'scenario': scenario, 'recorded_version': version,
            'recorded_date': str(datetime.date.today()), 'version_source': 'actual --version stdout',
            'version_output': version_output, 'source': 'private ephemeral shim JSONL (removed after capture)',
            'source_sha256': hashlib.sha256(raw).hexdigest(), 'steps': steps,
            'milestones': [{**m, 'ms': m['ms'] - baseline} for m in marks],
            'command': redactor.clean(command), 'interception': 'PATH shim, no forwarding; temporary configuration/auth copies',
            'redaction': 'Private paths, identities, session/tool IDs and nonempty content replaced; keys, types, empties, events, timing retained.',
            'limitations': spec.get('limitations', 'Hook-only replay; no transcript, quiet detection or subagents.'),
            'complete': result == 'recorded'}, str(original_home), username)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('agent')
    parser.add_argument('--scenario')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    recipe = load_recipe(args.agent)
    output = args.out or Path(tempfile.mkdtemp(prefix='hc-recordings-', dir='/tmp'))
    if args.scenario and args.scenario not in recipe['scenarios']:
        parser.error('unknown scenario: ' + args.scenario)
    scenarios = [args.scenario] if args.scenario else list(recipe['scenarios'])
    failed = False
    for scenario in scenarios:
        result = record(recipe, scenario, output / scenario)
        print(f'{args.agent}/{scenario}: {result} ({output / scenario})')
        failed |= result == 'needs-attention'
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
