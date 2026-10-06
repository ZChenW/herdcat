#!/usr/bin/env python3
"""Bounded async focus jobs, including timeout, overflow and process reaping."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_settled, wait_until

binary = str(Path('build/test_focus').resolve())
with tempfile.TemporaryDirectory(prefix='herdcat-focus-') as tmp:
    root = Path(tmp)
    with socket.socket(socket.AF_UNIX) as sock:
        sock.bind(str(root / 'niri.sock'))
        fake = root / 'niri'
        fake.write_text('''#!/usr/bin/python3
import json,os,sys,time
from pathlib import Path
root=Path(os.environ['FOCUS_FIXTURE'])
(root/'child').write_text(str(os.getpid()))
mode=os.environ['FOCUS_MODE']
if mode=='timeout':time.sleep(10)
if mode=='overflow':print('x'*70000);sys.exit(0)
if mode=='error':sys.exit(1)
if sys.argv[1:]==['msg','-j','windows']:
    if mode=='truncated':print('[{');sys.exit(0)
    print(json.dumps([{'id':123,'pid':int(os.environ['FOCUS_PID']) if mode!='missing' else 2147483647}]))
else:
    (root/'action').write_text(' '.join(sys.argv[1:]))
''')
        fake.chmod(0o700)
        env = runtime_env(NIRI_SOCKET=str(root / 'niri.sock'),
                   PATH=tmp + ':' + os.environ['PATH'], FOCUS_FIXTURE=tmp,
                   FOCUS_PID=str(os.getpid()))
        for mode, expected in [('ok', 1), ('missing', 2), ('truncated', 3),
                               ('error', 3), ('overflow', 3), ('timeout', 3)]:
            start = time.monotonic()
            result = subprocess.run([binary, str(os.getpid())],
                                    env=dict(env, FOCUS_MODE=mode),
                                    capture_output=True, text=True, timeout=2)
            assert result.returncode == 0 and result.stdout.strip() == str(expected), (mode, result)
            elapsed = time.monotonic() - start
            assert elapsed < 1.6 and (mode != 'timeout' or elapsed >= .9), elapsed
            assert not Path('/proc', (root / 'child').read_text()).exists()
        assert (root / 'action').read_text() == 'msg action focus-window --id 123'
print('Async focus success, missing target, parse errors, overflow, timeout and reaping passed.')
