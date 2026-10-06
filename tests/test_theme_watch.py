"""Exercise only a fake busctl; never contact a desktop portal."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path('build/test_theme_watch').resolve()
for fallback, reconnect in ((False, False), (True, False), (False, True)):
    with tempfile.TemporaryDirectory(prefix='herdcat-theme-') as tmp:
        root = Path(tmp)
        signal = json.dumps({'type': 'signal',
            'interface': 'org.freedesktop.portal.Settings',
            'member': 'SettingChanged', 'payload': {'type': 'ssv', 'data':
                ['org.freedesktop.appearance', 'color-scheme', {'type': 'u', 'data': 2}]}})
        script = root / 'busctl'
        script.write_text(f'''#!{sys.executable}
import os, sys, time
os.set_blocking(1, True)
previous = open(os.environ['THEME_LOG']).read() if os.path.exists(os.environ['THEME_LOG']) else ''
with open(os.environ['THEME_LOG'] + '.pids', 'a') as f:
    f.write(str(os.getpid()) + '\\n')
with open(os.environ['THEME_LOG'], 'a') as f:
    f.write(' '.join(sys.argv[1:]) + '\\n')
if 'monitor' in sys.argv:
    if {reconnect!r} and 'monitor' not in previous: sys.exit(1)
    time.sleep(.25)
    print({'x' * 66000!r}, flush=True)
    print({signal!r}, flush=True)
    time.sleep(10)
elif 'ReadOne' in sys.argv:
    if {fallback!r}: sys.exit(1)
    print('{{"type":"v","data":[{{"type":"u","data":1}}]}}')
else:
    print('{{"type":"v","data":[{{"type":"v","data":[{{"type":"u","data":1}}]}}]}}')
''')
        script.chmod(0o700)
        env = dict(os.environ, PATH=tmp, THEME_LOG=str(root / 'log'))
        subprocess.run([binary, 'reconnect' if reconnect else 'fake'], env=env, check=True)
        calls = (root / 'log').read_text().splitlines()
        # On an immediate monitor failure, the first ReadOne may be killed
        # before the fake executable runs; either ordering is valid.
        assert len(calls) in ((3, 4) if reconnect else (3,) if fallback else (2,)), calls
        assert sum('monitor' in c for c in calls) == (2 if reconnect else 1)
        assert any('ReadOne ss org.freedesktop.appearance color-scheme' in c for c in calls)
        for pid in (root / 'log.pids').read_text().splitlines():
            try:
                os.kill(int(pid), 0)
            except ProcessLookupError:
                continue
            raise AssertionError(f'Child {pid} was not reaped')
with tempfile.TemporaryDirectory(prefix='herdcat-theme-missing-') as tmp:
    (Path(tmp) / 'busctl').mkdir()
    subprocess.run([binary, 'missing'], env=dict(os.environ, PATH=tmp), check=True)
print('Fake busctl: initial read, Read fallback, long-line recovery, signal, retry, reaping, missing tool, explicit themes passed')
