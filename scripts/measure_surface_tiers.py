#!/usr/bin/env python3
"""Compare stage-33 release binaries with the existing /proc sampler.

Outside-sandbox only. Fresh isolated fixture per sample, three repetitions,
0/1/5/10 idle-visible sessions, default 110px cat, 2x output. No desktop access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import statistics
import subprocess
import tempfile
import time

from runtime_test_helpers import runtime_env, wait_until

ROOT = Path(__file__).resolve().parent.parent


def sample(binary, count, style, seconds, warmup):
    with tempfile.TemporaryDirectory(prefix='herdcat-tier-measure-') as directory:
        root = Path(directory)
        env = runtime_env(XDG_RUNTIME_DIR=directory, XDG_STATE_HOME=directory,
                          WAYLAND_DISPLAY='wayland-test', HERDCAT_TEST_MEASURE='1',
                          HERDCAT_TEST_TIER_SCALE='240')
        env.pop('NIRI_SOCKET', None)
        config = root / 'cat.conf'
        config.write_text(
            'monitor=TEST-1\ncat_height=110\noverlay_height=120\n'
            'overlay_opacity=0\noverlay_position=bottom\nsign_max=10\n'
            f'sign_style={style}\nsign_animations=full\nsign_idle=always\n'
            'idle_sleep_timeout=0\nenable_scheduled_sleep=0\n'
            'disable_fullscreen_hide=1\nagent_stale_timeout=0\n'
            'hotplug_scan_interval=3600\nenable_debug=0\n'
            'keyboard_device=/dev/input/herdcat-measure-nonexistent\n')
        server_log, app_log = root / 'server.log', root / 'app.log'
        with server_log.open('w') as server_file, app_log.open('w') as app_file:
            server = subprocess.Popen([str(ROOT / 'build/compositor/server')],
                                      env=env, stdout=server_file, stderr=server_file)
            app = None

            def diagnostics():
                return server_log.read_text()[-3000:] + app_log.read_text()[-2000:]

            try:
                wait_until(lambda: (root / 'wayland-test').exists(), 6,
                           diagnostics=diagnostics)
                app = subprocess.Popen([str(binary), '-c', str(config)], env=env,
                                       stdout=app_file, stderr=app_file)
                wait_until(lambda: (root / 'herdcat.sock').exists(), 6,
                           diagnostics=diagnostics)
                for key in range(1, count + 1):
                    for state in ('working', 'idle'):
                        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as control:
                            control.settimeout(3)
                            control.connect(str(root / 'herdcat.sock'))
                            control.sendall(f'ev claude {state} {key:016x} 0'.encode())
                            assert control.recv(512).startswith(b'0 ')
                time.sleep(warmup)
                result = subprocess.run([str(ROOT / 'scripts/measure_idle.sh'),
                                         str(app.pid), str(seconds)],
                                        text=True, capture_output=True, check=True)
                metrics = {}
                for line in result.stdout.splitlines():
                    key, separator, value = line.partition('=')
                    if separator:
                        metrics[key] = value
                dimensions = [tuple(map(int, line.split()[2].split('x')))
                              for line in server_log.read_text().splitlines()
                              if line.startswith('commit TEST-1 ')]
                assert dimensions and app.poll() is None, diagnostics()
                width, height = dimensions[-1]
                return dict(buffer_bytes=2 * width * height * 4,
                            dimensions=[width, height],
                            pss_kib=int(metrics['pss_kib']),
                            cpu_percent=float(metrics['cpu_percent']),
                            sampler_stdout=result.stdout)
            finally:
                for process in (app, server):
                    if process and process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, default=ROOT / 'build/release/herdcat')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--warmup', type=float, default=12)
    args = parser.parse_args()
    if args.seconds <= 0 or args.warmup < 12:
        parser.error('seconds must be positive; warmup must be at least 12s')
    before, after = args.before.resolve(), args.after.resolve()
    report = dict(method='measure_idle.sh: renderer /proc ticks, smaps_rollup PSS',
                  binary_sha256={name: hashlib.sha256(path.read_bytes()).hexdigest()
                                 for name, path in (('before', before), ('after', after))},
                  seconds=args.seconds, warmup=args.warmup, rounds=3,
                  complete=False, samples=[], medians=[])
    for style in ('fan', 'post'):
        for count in (0, 1, 5, 10):
            results = {'before': [], 'after': []}
            for repeat in range(3):
                for label, binary in (('before', before), ('after', after)):
                    result = sample(binary, count, style, args.seconds, args.warmup)
                    results[label].append(result)
                    report['samples'].append(dict(style=style, count=count,
                                                  repeat=repeat, binary=label, **result))
                    args.output.write_text(json.dumps(report, indent=2) + '\n')
            medians = {label: {metric: statistics.median(item[metric] for item in rows)
                               for metric in ('buffer_bytes', 'pss_kib', 'cpu_percent')}
                       for label, rows in results.items()}
            report['medians'].append(dict(style=style, count=count, **medians))
    report['complete'] = True
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    regressions = [row for row in report['medians']
                   if row['after']['cpu_percent'] > row['before']['cpu_percent']]
    print(json.dumps(report['medians'], indent=2))
    if regressions:
        raise SystemExit('FAIL: idle CPU median regressed; inspect raw samples.')
    print('Surface tier measurement CPU medians did not regress.')


if __name__ == '__main__':
    main()
