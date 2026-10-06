#!/bin/sh
# Read-only Linux process sampling. CPU 100% means one fully occupied core.
# Python's monotonic clock avoids wall-clock changes and sleep rounding errors.
set -eu
exec python3 - "$@" <<'PY'
import math
import os
from pathlib import Path
import re
import sys
import time


def fail(message):
    print(f"measure_idle: {message}", file=sys.stderr)
    sys.exit(1)


if len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help"):
    print("Usage: scripts/measure_idle.sh PID SECONDS\n"
          "Read /proc only; SECONDS must be positive (fractions allowed).\n"
          "CPU 100% = one core; memory is in KiB; top VMAs sort by RSS.\n"
          "Counters cover the given process, excluding child processes.")
    sys.exit(0)
if len(sys.argv) != 3:
    fail("usage: scripts/measure_idle.sh PID SECONDS")
if not re.fullmatch(r"[0-9]+", sys.argv[1]) or int(sys.argv[1]) <= 0:
    fail("PID must be a positive integer")
try:
    seconds = float(sys.argv[2])
except ValueError:
    fail("SECONDS must be a positive finite number")
if not math.isfinite(seconds) or seconds <= 0:
    fail("SECONDS must be a positive finite number")

pid = int(sys.argv[1])
proc = Path("/proc") / str(pid)


def stat():
    # comm can contain whitespace and ')', so splitting the whole line is wrong.
    fields = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    if fields[0] in ("Z", "X", "x"):
        fail("process exited during sampling")
    return int(fields[19]), int(fields[11]), int(fields[12])


def counters():
    start, user_ticks, system_ticks = stat()
    status = {}
    for line in (proc / "status").read_text().splitlines():
        key, _, value = line.partition(":")
        status[key] = value.strip()
    switches = tuple(int(status[key]) for key in (
        "voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"))
    rss = int(status["VmRSS"].split()[0])
    if stat()[0] != start:
        fail("PID was reused during sampling")
    return start, user_ticks + system_ticks, switches, rss, user_ticks, system_ticks


def memory():
    pss = None
    for line in (proc / "smaps_rollup").read_text().splitlines():
        if line.startswith("Pss:"):
            pss = int(line.split()[1])
    if pss is None:
        fail("Pss is missing from smaps_rollup")
    mappings = []
    header = re.compile(r"^([0-9a-f]+-[0-9a-f]+)\s+(\S+)\s+"
                        r"[0-9a-f]+\s+\S+\s+\d+\s*(.*)$")
    with (proc / "smaps").open() as source:
        for line in source:
            match = header.match(line)
            if match:
                mapping = dict(address=match[1], permissions=match[2],
                               path=match[3] or "[anonymous]", Size=0, Rss=0,
                               Pss=0)
                mappings.append(mapping)
            else:
                key, _, value = line.partition(":")
                if key in ("Size", "Rss", "Pss"):
                    mapping[key] = int(value.split()[0])
    mappings.sort(key=lambda item: (item["Rss"], item["Pss"], item["Size"]),
                  reverse=True)
    return pss, mappings[:8]


try:
    ticks_per_second = os.sysconf("SC_CLK_TCK")
    before = counters()
    began = time.monotonic()
    time.sleep(seconds)
    after = counters()
    ended = time.monotonic()
    elapsed = ended - began
    if after[0] != before[0]:
        fail("PID was reused during sampling")
    pss, mappings = memory()
    if stat()[0] != before[0]:
        fail("PID was reused during sampling")
    cpu_ticks = after[1] - before[1]
    user_ticks, system_ticks = after[4] - before[4], after[5] - before[5]
    voluntary, involuntary = (end - start for start, end in
                             zip(before[2], after[2]))
    if min(user_ticks, system_ticks, voluntary, involuntary) < 0:
        fail("process counters moved backwards")
except (OSError, ValueError, KeyError, IndexError) as error:
    fail(f"cannot sample /proc/{pid}: {error}")
except OverflowError:
    fail("SECONDS is too large")
except KeyboardInterrupt:
    fail("sampling interrupted")

print(f"pid={pid}")
print(f"starttime_ticks={before[0]}")
print(f"requested_seconds={seconds:g}")
print(f"elapsed_seconds={elapsed:.6f}")
print(f"sample_start_monotonic={began:.9f}")
print(f"sample_end_monotonic={ended:.9f}")
print(f"clock_ticks_per_second={ticks_per_second}")
print(f"cpu_ticks_delta={cpu_ticks}")
print(f"cpu_percent={100 * cpu_ticks / ticks_per_second / elapsed:.3f}")
print(f"user_cpu_ticks_delta={user_ticks}")
print(f"system_cpu_ticks_delta={system_ticks}")
print(f"user_cpu_percent={100 * user_ticks / ticks_per_second / elapsed:.3f}")
print(f"system_cpu_percent={100 * system_ticks / ticks_per_second / elapsed:.3f}")
print(f"voluntary_context_switches_delta={voluntary}")
print(f"involuntary_context_switches_delta={involuntary}")
print(f"context_switches_per_second={(voluntary + involuntary) / elapsed:.3f}")
print(f"rss_kib={after[3]}")
print(f"pss_kib={pss}")
print("largest_mappings_by_rss (individual VMAs; all sizes in KiB):")
print(" RSS_KiB  PSS_KiB SIZE_KiB PERMS ADDRESS PATH")
for item in mappings:
    print(f"{item['Rss']:8d} {item['Pss']:8d} {item['Size']:8d} "
          f"{item['permissions']} {item['address']} {item['path']}")
PY
