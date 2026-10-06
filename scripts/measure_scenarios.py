#!/usr/bin/env python3
"""Sample a release renderer on the test-runtime compositor, never the desktop."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
CONFIG = """monitor=TEST-1
cat_height=110
overlay_height=120
overlay_opacity=0
overlay_position=bottom
fps=60
sign_style=fan
sign_animations=full
sign_language=zh
sign_font=
sign_idle=hover
idle_sleep_timeout=0
enable_scheduled_sleep=0
disable_fullscreen_hide=1
hotplug_scan_interval=3600
agent_stale_timeout=0
keyboard_device=/dev/input/herdcat-measure-nonexistent
enable_debug=0
"""


def positive(value):
    number = float(value)
    if not 0 < number < float("inf"):
        raise argparse.ArgumentTypeError("must be positive and finite")
    return number


def wait_for(condition, processes):
    until = time.monotonic() + 5
    while time.monotonic() < until:
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"test process {process.pid} exited early")
        if condition():
            return
        time.sleep(0.02)
    raise RuntimeError("fixture readiness timed out")


def wire(directory, request):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
        client.settimeout(3)
        client.connect(str(directory / "herdcat.sock"))
        client.sendall(request.encode())
        reply = client.recv(4096).decode()
    if not reply.startswith("0 "):
        raise RuntimeError(f"{request}: {reply}")
    return reply


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def fixture_sample(log, metrics):
    """Count compositor receipts inside the sampler's monotonic interval."""
    start, end = metrics['sample_start_monotonic'], metrics['sample_end_monotonic']
    events = {kind: [] for kind in ('submit', 'release', 'frame-request', 'frame-done')}
    for line in log.splitlines():
        fields = line.split()
        if len(fields) != 3 or fields[0] not in events or fields[1] != 'TEST-1':
            continue
        when = float(fields[2])
        if start <= when < end:
            events[fields[0]].append(when)
    commits = events['submit']
    intervals = [(b - a) * 1000 for a, b in zip(commits, commits[1:])]
    return dict(buffer_commits_in_sample=len(commits),
                buffer_commits_per_second=len(commits) / (end - start),
                frame_requests_in_sample=len(events['frame-request']),
                frame_callbacks_in_sample=len(events['frame-done']),
                buffer_releases_in_sample=len(events['release']),
                buffer_releases_per_second=len(events['release']) / (end - start),
                submission_intervals_ms=dict(min=min(intervals),
                                             median=statistics.median(intervals),
                                             max=max(intervals)) if intervals else None,
                fixture_events_ms={kind: [round((when - start) * 1000, 6)
                                         for when in times]
                                   for kind, times in events.items()})


def warm_font_panel(server, log, processes):
    """Exercise font previews using only our private compositor's pointer."""
    def region():
        regions = [tuple(map(int, line.split()[2:]))
                   for line in log.read_text().splitlines()
                   if line.startswith('input TEST-1 ')]
        return regions[-1] if regions else (0, 0, 0, 0)

    def send(command):
        server.stdin.write(command + '\n')
        server.stdin.flush()

    wait_for(lambda: region()[2] > 0, processes)
    cat = region()
    x, y = cat[0] + cat[2] // 2, cat[1] + cat[3] // 2
    send(f'tap TEST-1 {x} {y} 273')
    wait_for(lambda: region() != cat and region()[1] + region()[3] <= cat[1] + 2,
             processes)
    time.sleep(.4)
    card = region()
    send(f'tap TEST-1 {card[0] + card[2] * 77 // 154} '
         f'{card[1] + card[3] * 103 // 168} 272')
    wait_for(lambda: 'overlay TEST-1 herdcat-font-panel' in log.read_text(), processes)
    time.sleep(2)
    send(f'tap TEST-1 {x} {y} 273')
    wait_for(lambda: 'gone herdcat-font-panel' in log.read_text(), processes)
    send('out TEST-1')
    time.sleep(.4)


def sample(binary, fixture, scenario, seconds, warmup, *, font=None,
           working=False, manual=False, frame_ms=17, release_ms=16,
           profile_dir=None, font_panel=False, focus_depth=None,
           release_to=None):
    with tempfile.TemporaryDirectory(prefix="herdcat-measure-") as temporary:
        directory = Path(temporary)
        env = dict(os.environ, XDG_RUNTIME_DIR=temporary,
                   XDG_STATE_HOME=temporary, WAYLAND_DISPLAY="wayland-test",
                   XDG_CACHE_HOME=str(directory / "cache"),
                   HERDCAT_TEST_MEASURE="1",
                   HERDCAT_TEST_FRAME_MS=str(frame_ms),
                   HERDCAT_TEST_RELEASE_MS=str(release_ms))
        if profile_dir is not None:
            profile_dir.mkdir(parents=True, exist_ok=True)
            env['GMON_OUT_PREFIX'] = str(profile_dir.resolve() / 'gmon')
        for key in ("NIRI_SOCKET", "HYPRLAND_INSTANCE_SIGNATURE", "WAYLAND_DEBUG",
                    "HERDCAT_TEST_DRAG"):
            env.pop(key, None)
        if font_panel:
            env['HERDCAT_TEST_DRAG'] = '1'
        config = directory / "measure.conf"
        config_text = CONFIG
        if font is not None:
            config_text = config_text.replace('sign_font=\n', f'sign_font={font}\n')
        config.write_text(config_text)
        server_path, app_path = directory / "server.log", directory / "app.log"
        app = server = None
        focus = None
        helpers = []
        helper_stopped = False
        try:
            if focus_depth is not None:
                from measure_focus_fixture import FocusFixture
                focus = FocusFixture(directory, focus_depth).start()
                env['NIRI_SOCKET'] = str(focus.path)
            with server_path.open("w") as server_log, app_path.open("w") as app_log:
                server = subprocess.Popen([fixture], env=env,
                                          stdin=subprocess.PIPE if font_panel else subprocess.DEVNULL,
                                          stdout=server_log, stderr=server_log, text=True)
                wait_for(lambda: (directory / "wayland-test").exists(), [server])
                app = subprocess.Popen([binary, "-c", str(config)], env=env,
                                       cwd=directory,
                                       stdout=app_log, stderr=app_log)
                wait_for(lambda: (directory / "herdcat.sock").exists(), [server, app])
                wait_for(lambda: "commit TEST-1 2560x" in server_path.read_text(),
                         [server, app])
                if font_panel:
                    warm_font_panel(server, server_path, [server, app])
                if scenario != "idle":
                    if manual:
                        wire(directory, f"state {scenario}")
                    else:
                        wire(directory, f"ev claude {scenario} aaaaaaaaaaaaaaaa 0")
                        wire(directory, "name aaaaaaaaaaaaaaaa 测量")
                if working:
                    pid = focus.agent_pid if focus is not None else 0
                    wire(directory, f"ev claude working bbbbbbbbbbbbbbbb {pid}")
                    wire(directory, "name bbbbbbbbbbbbbbbb Claude 测量")
                time.sleep(warmup)
                status = wire(directory, "status")
                sessions = wire(directory, "sessions")
                if f"agent={scenario}" not in status:
                    raise RuntimeError(f"unexpected state: {status}")
                if scenario == "idle":
                    assert "sessions=0" in status
                else:
                    expected = 2 if working else 1
                    assert f"sessions={expected}" in status
                    assert f" {scenario} " in sessions
                    if working:
                        assert " working " in sessions
                helpers = [int(pid) for pid in Path(
                    f"/proc/{app.pid}/task/{app.pid}/children").read_text().split()]
                if len(helpers) != 1:
                    raise RuntimeError(f"expected one input helper: {helpers}")
                if b"--input-helper" not in Path(
                        f"/proc/{helpers[0]}/cmdline").read_bytes().split(b"\0"):
                    raise RuntimeError("child is not this renderer's input helper")
                # With no devices, scan_interval=0 exits and the renderer retries.
                # Quiesce only our own helper so neither input nor retry wakes
                # contaminate the renderer sample. Resume it before shutdown.
                os.kill(helpers[0], signal.SIGSTOP)
                helper_stopped = True
                wait_for(lambda: Path(f"/proc/{helpers[0]}/stat").read_text()
                         .rsplit(")", 1)[1].split()[0] == "T", [server, app])
                for descriptor in Path(f"/proc/{helpers[0]}/fd").iterdir():
                    if os.readlink(descriptor).startswith("/dev/input/"):
                        raise RuntimeError("measurement opened a real input device")
                commits_before = server_path.read_text().count("commit TEST-1 ")
                measurement = subprocess.run(
                    [str(ROOT / "scripts/measure_idle.sh"), str(app.pid), str(seconds)],
                    text=True, capture_output=True, timeout=seconds + 10, check=True)
                commits_after = server_path.read_text().count("commit TEST-1 ")
                status_after = wire(directory, "status")
                sessions_after = wire(directory, "sessions")
                if f"agent={scenario}" not in status_after:
                    raise RuntimeError(f"state changed during sampling: {status_after}")
                metrics = {}
                for line in measurement.stdout.splitlines():
                    key, separator, value = line.partition("=")
                    if not separator:
                        break
                    metrics[key] = float(value) if "." in value else int(value)
                result = dict(scenario=scenario, metrics=metrics,
                              config=config_text, working=working, manual=manual,
                              frame_ms=frame_ms, release_ms=release_ms,
                              font_panel_warmed=font_panel,
                              focus_ancestry_depth=focus_depth,
                              measurement=measurement.stdout,
                              buffer_commits_delta=commits_after - commits_before,
                              status=status, sessions=sessions,
                              status_after=status_after, sessions_after=sessions_after)
                result.update(fixture_sample(server_path.read_text(), metrics))
                if release_to is not None:
                    request = (f'state {release_to}' if manual else
                               f'ev claude {release_to} aaaaaaaaaaaaaaaa 0')
                    wire(directory, request)
                    idle_at = time.monotonic()
                    points = []
                    # No control queries during the 60-second grace interval:
                    # they would wake the renderer and hide a missing deadline.
                    for offset in (3, 58, 62, 68):
                        while time.monotonic() < idle_at + offset:
                            if app.poll() is not None or server.poll() is not None:
                                raise RuntimeError('cache release fixture exited early')
                            time.sleep(max(0, min(.2, idle_at + offset - time.monotonic())))
                        measured = subprocess.run(
                            [str(ROOT / 'scripts/measure_idle.sh'), str(app.pid), '1'],
                            text=True, capture_output=True, timeout=11, check=True)
                        values = {}
                        for line in measured.stdout.splitlines():
                            key, separator, value = line.partition('=')
                            if not separator:
                                break
                            values[key] = float(value) if '.' in value else int(value)
                        switches = {}
                        for line in Path(f'/proc/{app.pid}/status').read_text().splitlines():
                            key, _, value = line.partition(':')
                            if key in ('voluntary_ctxt_switches', 'nonvoluntary_ctxt_switches'):
                                switches[key] = int(value)
                        points.append(dict(offset_seconds=offset, metrics=values,
                                           switches=switches, measurement=measured.stdout))
                    assert points[1]['metrics']['sample_end_monotonic'] < idle_at + 60
                    result['cache_release'] = dict(target=release_to,
                                                   no_waiting_since=idle_at,
                                                   points=points)
                    assert f'agent={release_to}' in wire(directory, 'status')
                    # Record the compositor receipt time, not the polling
                    # observer's readiness time, for a cold waiting return.
                    started = time.monotonic()
                    request = ('state waiting' if manual else
                               'ev claude waiting aaaaaaaaaaaaaaaa 0')
                    wire(directory, request)

                    def returned_submissions():
                        return [float(line.split()[2]) for line in server_path.read_text().splitlines()
                                if line.startswith('submit TEST-1 ') and
                                float(line.split()[2]) >= started]

                    wait_for(lambda: bool(returned_submissions()), [server, app])
                    result['cache_release']['waiting_return_submit_ms'] = (
                        returned_submissions()[0] - started) * 1000
                    assert 'agent=waiting' in wire(directory, 'status')
                os.kill(helpers[0], signal.SIGCONT)
                helper_stopped = False
                app.terminate()
                if app.wait(timeout=3) != 0:
                    raise RuntimeError("renderer did not exit cleanly")
                if any(Path(f"/proc/{pid}").exists() for pid in helpers):
                    raise RuntimeError("input helper survived renderer cleanup")
                server.terminate()
                if server.wait(timeout=3) != 0:
                    raise RuntimeError("compositor did not exit cleanly")
                lines = server_path.read_text().splitlines()
                result.update(clean_exit=True, helper_reaped=True,
                              input_helper_quiesced=True,
                              buffer_dimensions=sorted(set(
                                  line.split()[2] for line in lines
                                  if line.startswith("commit "))),
                              fixture_evidence=[line for line in lines if
                                                line.startswith(("ready", "overlay ",
                                                                 "phase ", "totals ",
                                                                 "measure timing "))],
                              app_log=app_path.read_text())
                if profile_dir is not None:
                    profile = profile_dir / f'gmon.{app.pid}'
                    if not profile.is_file():
                        raise RuntimeError('renderer did not write its gprof profile')
                    result['gprof_profile'] = str(profile.resolve())
                return result
        except Exception:
            for log in (app_path, server_path):
                if log.exists():
                    print(log.read_text())
            raise
        finally:
            if helper_stopped and app is not None and app.poll() is None:
                try:
                    os.kill(helpers[0], signal.SIGCONT)
                except ProcessLookupError:
                    pass
            stop(app)
            stop(server)
            if focus is not None:
                focus.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=positive, default=30)
    parser.add_argument("--warmup", type=positive, default=3)
    parser.add_argument("--runs", type=int, choices=range(1, 11), default=1)
    parser.add_argument("--output", type=Path, required=True,
                        help="JSON artifact including raw samples and fixture evidence")
    parser.add_argument('--font', help='override only the isolated sign font')
    parser.add_argument('--working', action='store_true',
                        help='add a second working session to the waiting sample')
    parser.add_argument('--manual', action='store_true',
                        help='use the manual state instead of a hook session')
    parser.add_argument('--font-panel', action='store_true',
                        help='open and close font previews before sampling, only in the fixture')
    parser.add_argument('--focus-depth', type=int, choices=range(9),
                        help='use an owned PTY agent and private Niri stream with N ancestors')
    parser.add_argument('--frame-ms', type=int, choices=range(1, 1001), default=17)
    parser.add_argument('--release-ms', type=int, choices=range(1, 1001), default=16)
    parser.add_argument('--scenario', choices=['idle', 'working', 'waiting'],
                        help='sample only this scenario')
    parser.add_argument('--binary', type=Path,
                        help='alternate local binary, for profiling builds')
    parser.add_argument('--profile-dir', type=Path,
                        help='retain per-process gmon files outside the fixture')
    parser.add_argument('--release-to', choices=['idle', 'working'],
                        help='after waiting, measure cache memory across the 60 s idle grace')
    args = parser.parse_args()
    binary, fixture = ROOT / "build/release/herdcat", ROOT / "build/compositor/server"
    if args.binary is not None:
        binary = args.binary.resolve()
    if args.working and args.scenario != 'waiting':
        parser.error('--working requires --scenario waiting')
    if args.focus_depth is not None and not args.working:
        parser.error('--focus-depth requires --working')
    if args.release_to is not None and (args.scenario != 'waiting' or args.working):
        parser.error('--release-to requires a single --scenario waiting')
    for path in (binary, fixture):
        if not path.is_file():
            parser.error("first run: make release compositor-test-build")
    if Path("/dev/input/herdcat-measure-nonexistent").exists():
        parser.error("the test-only input selector unexpectedly exists")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                       text=True).strip()
    source_diff = subprocess.check_output(
        ["git", "diff", "HEAD", "--", "src", "include", "protocols", "lib", "Makefile"],
        cwd=ROOT, text=True)
    report = dict(created_utc=datetime.now(timezone.utc).isoformat(),
                  source_revision=revision, source_diff=source_diff,
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
                  sampler_sha256=hashlib.sha256(
                      (ROOT / "scripts/measure_idle.sh").read_bytes()).hexdigest(),
                  config=CONFIG, seconds=args.seconds, warmup_seconds=args.warmup,
                  cpu_scope="renderer only, including its threads; excludes helper",
                  context_switch_scope="renderer main thread (/proc/PID/status)",
                  submission_scope="compositor TEST-1 buffer receipts within the "
                                   "sampler's CLOCK_MONOTONIC interval",
                  complete=False, samples=[])
    for run in range(1, args.runs + 1):
        for scenario in ([args.scenario] if args.scenario else
                         ["idle", "working", "waiting"]):
            print(f"Run {run}: {scenario}", flush=True)
            result = sample(str(binary), str(fixture), scenario, args.seconds,
                            args.warmup, font=args.font, working=args.working,
                            manual=args.manual, frame_ms=args.frame_ms,
                            release_ms=args.release_ms,
                            profile_dir=args.profile_dir, font_panel=args.font_panel,
                            focus_depth=args.focus_depth, release_to=args.release_to)
            result["run"] = run
            report["samples"].append(result)
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
            print(result["measurement"], flush=True)
    report["complete"] = True
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"Saved {args.output}")


if __name__ == "__main__":
    main()
