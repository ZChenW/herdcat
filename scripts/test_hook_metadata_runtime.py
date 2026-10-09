#!/usr/bin/env python3
"""Late hook registration and persistence on a private Wayland fixture."""
from pathlib import Path
import socket
import subprocess
import tempfile

from runtime_test_helpers import HookParent, runtime_env, run_on_pty, wait_until

run_on_pty()
binary = str(Path('build/herdcat').resolve())
fixture = str(Path('build/compositor/server').resolve())

with tempfile.TemporaryDirectory(prefix='hc-meta-') as directory:
    root = Path(directory)
    env = runtime_env(HOME=directory, XDG_RUNTIME_DIR=directory,
                      XDG_STATE_HOME=directory, XDG_CONFIG_HOME=directory,
                      WAYLAND_DISPLAY='wayland-test', XDG_CURRENT_DESKTOP='test')
    project = root / 'late-project'
    project.mkdir()
    # Stop project discovery at the fixture, independent of ancestor .git files.
    (project / '.git').mkdir()
    config = root / 'cat.conf'
    config.write_text('keyboard_device=/dev/input/herdcat-test-nonexistent\n'
                      'monitor=TEST-1\nfps=1\nhotplug_scan_interval=0\n'
                      'agent_stale_timeout=0\n')
    log = (root / 'runtime.log').open('w+')
    server = subprocess.Popen([fixture], env=env, stdout=log, stderr=log)
    app = parent = None

    def wait(condition):
        return wait_until(condition, 3, description='late metadata runtime',
                          diagnostics=lambda: (root / 'runtime.log').read_text()[-4000:])

    def wire(request, success=True):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
            client.settimeout(2)
            client.connect(str(root / 'herdcat.sock'))
            client.sendall(request.encode())
            response = client.recv(4096).decode()
        assert response.startswith('0 ') == success, (request, response)
        return response

    def start():
        global app
        app = subprocess.Popen([binary, '-c', str(config)], env=env,
                               stdout=log, stderr=log)
        wait(lambda: (root / 'herdcat.sock').exists())
        return app

    def sessions():
        result = subprocess.run([binary, '--sessions'], env=env,
                                capture_output=True, text=True, timeout=3)
        assert result.returncode == 0 and not result.stderr, result
        return result.stdout

    def stop(process):
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)

    try:
        wait(lambda: (root / 'wayland-test').exists())
        app = start()
        parent = HookParent(binary, env)
        payload = dict(session_id='late-codex', cwd=str(project))
        # No start or prompt event precedes this approval request.
        parent.invoke('codex', payload, 'PermissionRequest')
        text = sessions()
        assert text.split()[2] == 'waiting' and text.rstrip().endswith('late-project'), text
        stored = root / 'herdcat/sessions'
        # A title delivered through the existing metadata path also survives.
        # Get the complete key from the persisted row, avoiding display prefixes.
        wait(lambda: stored.exists())
        key = stored.read_text().split()[1]
        assert wire(f'ttl {key} Synthetic title') == '0 ok'
        assert wire(f'ev codex waiting {key} {parent.process.pid}') == '0 ok'
        stop(app)
        assert app.returncode == 0
        data = stored.read_text()
        assert 'late-project' in data and 'Synthetic title' in data, data
        app = start()
        assert 'late-project' in sessions() and 'Synthetic title' in sessions()
        parent.invoke('codex', payload, 'PreToolUse')
        text = sessions()
        assert 'late-project' in text and 'Synthetic title' in text and text.split()[2] == 'working', text
        assert wire(f'ev codex working {key} {parent.process.pid}') == '0 ok'

        # A new session after restart, first seen through another tool event.
        parent.invoke('qwen', dict(session_id='late-qwen', cwd=str(project)), 'PreToolUse')
        text = sessions()
        assert text.count('late-project') == 2, text

        # Old hook / new daemon: ignore the extra response text, as old clients do.
        old_key = '123456789abcdef0'
        assert wire(f'ev opencode waiting {old_key} 0') == '0 ok metadata'
        assert wire(f'ev opencode working {old_key} 0') == '0 ok metadata'
        assert wire(f'cwd {old_key} {str(project).encode().hex()} late-project') == '0 ok'
        assert wire(f'ev opencode waiting {old_key} 0') == '0 ok'
        assert wire(f'ev opencode end {old_key} 0') == '0 ok'
        assert wire(f'ev opencode fail {old_key} 0') == '0 ok'
        wire('ev INVALID working 123456789abcdef0 0', success=False)
        print('Late approval/tool metadata, restart persistence and old wire client passed.')
    finally:
        if parent:
            parent.close()
        stop(app)
        stop(server)
        log.close()
