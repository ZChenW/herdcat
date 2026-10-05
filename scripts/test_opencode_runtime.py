#!/usr/bin/env python3
"""Optional opencode 2.x load/create/delete check, isolated HOME and no model use."""
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.parse
import urllib.request

executable = shutil.which('opencode')
if not executable:
    raise SystemExit('opencode unavailable; runtime check not performed')
binary = Path('build/herdcat').resolve()
plugin = Path('integrations/opencode').resolve()
with tempfile.TemporaryDirectory(prefix='bongo-opencode-runtime-') as directory:
    root = Path(directory)
    work = root / 'work'
    work.mkdir()
    (root / 'herdcat').symlink_to(binary)
    with socket.socket() as address:
        address.bind(('127.0.0.1', 0))
        port = address.getsockname()[1]
    env = dict(os.environ, HOME=str(root / 'home'),
               XDG_CONFIG_HOME=str(root / 'config'), XDG_STATE_HOME=str(root / 'state'),
               XDG_CACHE_HOME=str(root / 'cache'), XDG_DATA_HOME=str(root / 'data'),
               XDG_RUNTIME_DIR=directory,
               PATH=directory + os.pathsep + os.environ['PATH'],
               OPENCODE_CONFIG_CONTENT=json.dumps({'plugin': [str(plugin)]}),
               OPENCODE_CONFIG_PROJECT_DISABLE='true')
    for key in ('OPENCODE_CONFIG', 'OPENCODE_CONFIG_DIR', 'HERDCAT_HOOK_DEBUG'):
        env.pop(key, None)
    secret = {}
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as overlay:
        overlay.bind(str(root / 'herdcat.sock'))
        overlay.listen(16)
        overlay.settimeout(5)
        proc = subprocess.Popen([executable, 'serve', '--hostname', '127.0.0.1',
                                 '--port', str(port)], cwd=work, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True)
        def drain():
            for line in proc.stdout:
                if line.startswith('server password '):
                    secret['value'] = line.strip().split(' ', 2)[2]
                # Other service output is discarded, never printed or retained.
        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        def request(route, data=None, method=None):
            headers = {'Content-Type': 'application/json', 'Authorization': 'Basic ' +
                       base64.b64encode(('opencode:' + secret['value']).encode()).decode()}
            req = urllib.request.Request(f'http://127.0.0.1:{port}' + route,
                                         data=json.dumps(data).encode() if data is not None else None,
                                         headers=headers, method=method)
            with urllib.request.urlopen(req, timeout=5) as response:
                body = response.read()
                return json.loads(body) if body else None
        def receive():
            connection, _ = overlay.accept()
            with connection:
                text = connection.recv(64).decode()
                connection.sendall(b'0 ok')
                return text
        try:
            active = False
            query = '?' + urllib.parse.urlencode({'location[directory]': str(work)})
            for _ in range(60):
                try:
                    plugins = request('/api/plugin' + query)['data']
                    active = any(p['id'] == 'herdcat.sessions' and
                                 p['state']['status'] == 'active' for p in plugins)
                    if active:
                        break
                except (KeyError, OSError):
                    pass
                time.sleep(0.2)
            assert active, 'Isolated service did not load the v2 plugin'
            parent = request('/api/session', {'location': {'directory': str(work)}})['data']
            start = receive().split()
            assert start[:3] == ['ev', 'opencode', 'start'] and start[-1] == '0'
            key = start[3]
            assert receive() == f'name {key} work'
            child = request('/api/session', {'location': {'directory': str(work)},
                                             'parentID': parent['id']})['data']
            assert child['parentID'] == parent['id']
            # Allow the async metadata lookup to settle before deleting the child.
            overlay.settimeout(0.5)
            try:
                overlay.accept()
            except TimeoutError:
                pass
            else:
                raise AssertionError('Child session unexpectedly created a sign')
            request('/api/session/' + child['id'], method='DELETE')
            request('/api/session/' + parent['id'], method='DELETE')
            overlay.settimeout(5)
            assert receive() == f'ev opencode end {key} 0'
            print('Real opencode v2 loader, root/child handling, PID 0 and deletion passed.')
        finally:
            proc.terminate()
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            reader.join(2)
            print('Owned isolated opencode service stopped; personal config unchanged.')
