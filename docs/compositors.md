# Compositor focus backends

niri remains the default supported backend. `NIRI_SOCKET` takes precedence over
`HYPRLAND_INSTANCE_SIGNATURE`, then `SWAYSOCK`. Sway is selected whenever
`SWAYSOCK` is advertised, without an experimental opt-in. Only Hyprland is
unavailable unless `compositor_experimental=1` is set globally; no Hyprland
focus subprocess or event connection is attempted by default. No
advertised compositor leaves signs available, but focus, typing-desk tracking
and read acknowledgements unavailable.

Hyprland: **本机嵌套于 niri 26.04 的 IPC 验证通过，图层呈现验证失败**
(2026-10-07, Hyprland 0.56.2 / Aquamarine 0.15.1). niri rejects the outer
surface's buffer before its initial configure acknowledgement. The six IPC
checks pass, but the overlay frame callback does not return; full nested
acceptance remains incomplete. CI does not cover Hyprland. Sway:
**已在无头 Sway 1.12 上验证** (2026-10-07, headless/pixman).
All seven Sway real-compositor runtime assertions passed. Sway is supported without opt-in;
Hyprland remains experimental and opt-in.
Real Sway pointer clicks, multiple outputs and XWayland remain uncovered. Unit fixtures additionally check payloads, argv and gating.
niri CLI arguments, EventStream parsing,
terminal ancestry/selection and all previous niri test expectations are retained.

| Backend | Initial windows | Events | Window focus |
|---|---|---|---|
| niri | `niri msg -j windows` for clicks; EventStream initial list for tracking | `"EventStream"` on NIRI_SOCKET | `niri msg action focus-window --id ID` |
| Hyprland (experimental) | `hyprctl -j clients` then `hyprctl -j activewindow` | `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock` | `hyprctl dispatch focuswindow address:0xHEX`; Lua syntax fallback on its explicit rejection |
| Sway | IPC GET_TREE; `swaymsg -r -t get_tree` for clicks | IPC SUBSCRIBE `["window"]` | `swaymsg -r '[con_id=ID] focus'` |

Commands use separate argv entries, never a shell. Lua-configured Hyprland
0.56.2 explicitly rejects legacy dispatch syntax. Only that diagnostic permits
one retry using `hyprctl dispatch 'hl.dsp.focus({window="address:0xHEX"})'`,
with the same original deadline. Legacy configs keep their existing command;
unrelated errors and a failed Lua retry remain failures. Identifiers become checked
integers before command construction. The stream adapter normalizes focus,
window addition/change/removal, PID and title into the existing bounded map.
Titles remain truncated private metadata; they are neither logged nor saved by
the new backends. Hyprland activewindow/openwindow/title events that lack PIDs
trigger coalesced client snapshots, followed by activewindow; there is no idle
polling. Sway traverses tiling and floating nodes and handles fragmented IPC
headers/payloads, subscription failure and tree focus. The transport bounds
messages to 64 KiB and the map to 128 windows, with one-second setup/query
limits, partial sends, reconnect backoff and shutdown/reaping.

The old `src/platform/hyprland.c` detects fullscreen state, and is independent
of the compositor focus layer. Its implementation, activation policy and
regressions have not changed. Legacy Hyprland socket directories are not probed;
the experimental backend implements the current documented XDG runtime path.

Sources: [Hyprland IPC](https://wiki.hypr.land/IPC/),
[Hyprland hyprctl](https://wiki.hypr.land/Configuring/Using-hyprctl/),
[Sway's shipped IPC manual](https://man.archlinux.org/man/sway-ipc.7.en).
The Hyprland documentation specifies event/payload layouts rather than complete
window-event JSON samples; its test strings instantiate those layouts. The Sway
fixtures project the manual's WINDOW example (id 12, pid 19787, null name) and
GET_TREE example (urxvt id 5, pid 23959), with separate regression variants.

Before promoting Hyprland, a reviewer with that compositor must explicitly
enable it in an isolated test config and verify initial focus, window lifecycle,
title changes, click focus, unread acknowledgement, typing desk, reconnection,
shutdown and disabled opt-in. Stage 34 exercised a private headless Sway 1.12
instance; the remaining desktop acceptance is listed below.

## Headless Hyprland initialization blocked

Official Arch packages were unpacked under `/tmp` and tested without a GPU
session or a host display connection. Direct startup, forced llvmpipe and
surfaceless EGL each failed twice before creating a Wayland socket:
`Cannot open backend: no allocator available`. Aquamarine's headless backend
returns no DRM fd, while its primary GBM allocator requires one. An independent
probe of the shipped headless backend reproduced that failure twice.

Mesa surfaceless EGL successfully used llvmpipe twice. Nesting Hyprland inside
private headless Sway/pixman still failed twice: the parent does not advertise
the `zwp_linux_dmabuf_v1` protocol required by Aquamarine's Wayland backend.
The official [virtual GPU documentation](https://wiki.hypr.land/configuring/extra/virtual-gpu/)
describes a GPU without display outputs; `AQ_NO_KMS_REQUIREMENT=1` does not
provide a device-free allocator.

A subsequent local attempt used the system-installed `/usr/bin/Hyprland`
0.56.2 inside private headless Sway 1.12 / wlroots 0.20.1 with `gles2`, an
AMD render node and Mesa/radeonsi. The parent provided linux-dmabuf, but
Aquamarine 0.15.1 unconditionally bound `xdg_wm_base` version 6 while Sway
advertised version 5. Both repeats failed with `expected at most 5, got 6`,
then aborted before creating a Hyprland Wayland socket. Seat access was
blocked using a nonexistent private seatd socket; neither compositor used a
host display socket. No system library or compositor was patched.

Those device-free and private-Sway attempts remain blocked. The subsequent
niri attempt below validates the IPC backend, but not layer presentation. The
experimental default remains zero. See [the report](performance/hyprland-headless-report.md)
for versions, evidence, commands and limitations.

## Opt-in nested Hyprland runtime acceptance

`HERDCAT_HYPRLAND_NESTED=1 python3 scripts/test_hyprland_runtime.py` opens one
Hyprland window in the current Wayland desktop. Without that explicit variable,
including in `make test-runtime` and CI, it prints SKIP. Missing system
`/usr/bin/Hyprland`, `hyprctl`, or the host Wayland socket also skips. An enabled
test with available prerequisites fails on startup or assertion errors.

The host socket's absolute path is given only to Hyprland. All other clients,
herdcat commands and IPC use the test's private short mode-0700 `/tmp` runtime
and isolated HOME/XDG directories. No niri command is issued. XWayland, desktop
integration, crash reports, seat access and DRM device enumeration are disabled.
The test reuses the Sway xdg-toplevel fixture and layer roundtrip criterion.
An independent watchdog sends TERM to the Hyprland PID at 35 seconds and KILL
at 38; normal cleanup immediately terminates/reaps that PID. A locked `/tmp`
ledger, keyed by worktree path, reserves at most 15 launches across invocations.
Exhausting that budget fails before spawning. Optional `--evidence-dir` saves
private logs and launch records inside this worktree or `/tmp` after cleanup.

Against niri 26.04 / Hyprland 0.56.2, backend selection, PID-to-address mapping,
focus/unread acknowledgement, `--focus`, close/pidfd removal, experimental
reloading and clean shutdown pass. Configure/ack and buffer commits for the
herdcat layer occur, but its frame callback never returns. The outer connection
reports `must ack the initial configure before attaching buffer`, while
Hyprland keeps answering private IPC. The test retains that presentation
failure and collects the independent IPC checks during the same launch; its
exit remains nonzero. It does not substitute IPC success for rendering.

Real pointer clicks, typing input/desk rendering, multiple outputs, physical
Hyprland sessions, XWayland, title changes and compositor restart are uncovered.
The window budget permits brief verification, not a long-term stability claim.
No system compositor/library was patched. Full nested acceptance requires
resolving the external presentation failure and rerunning this strict test.

## Headless Sway runtime acceptance

Run `python3 scripts/test_sway_runtime.py` from any directory outside a socket
restricted sandbox. The script builds herdcat and a small xdg-toplevel fixture;
it requires `sway`, `swaymsg`, a C compiler, pkg-config, wayland-scanner and the
normal build dependencies. No terminal emulator, swaybar, swaybg, GPU or input
device is needed. `make test-runtime` includes it. Missing Sway prints `SKIP`
locally; `HERDCAT_REQUIRE_SWAY=1` makes this a failure, as in the Arch CI job.
An installed Sway that cannot start or a failed assertion always fails the test.

The test creates a short mode-0700 directory under `/tmp`, isolates runtime,
config, cache and state, removes host compositor/display metadata, and starts
Sway with `WLR_BACKENDS=headless`, `WLR_LIBINPUT_NO_DEVICES=1`,
`WLR_RENDERER=pixman` and one `HEADLESS-1` output. It tests layer configure and
buffer submission, backend selection, initial PID/con_id discovery, focus events
and unread acknowledgement, the `--focus` job used by sign clicks, window close
while its PID remains alive, pidfd removal, opt-in disable/re-enable and shutdown.
Every failure prints recent commands, live `swaymsg` version/output/tree results,
herdcat status/sessions, and herdcat, Sway and fixture log tails before cleanup.

Sway's GET_OUTPUTS/GET_TREE describe outputs and ordinary windows, not layer
surfaces. Layer acceptance therefore additionally requires Sway's debug message
for `herdcat-overlay`, and the corresponding real Wayland configure/ack,
non-null buffer attach, surface commit and returned frame callback. It does not
infer successful presentation from an outgoing commit alone. This is the
observability adjustment to stage 34's proposed IPC-only layer assertion.

`--status` reports `compositor=Sway focus-watch=ready` when the
subscription and initial tree are ready. With Sway or Hyprland selected, `--sessions` adds
`window-session KEY8 window=ID seen=yes|no` lines from the live focus map; zero
means no matching live window. Sway uses decimal con_ids; Hyprland uses
the decimal value of its hexadecimal address. These IDs are never persisted. The existing
session rows retain their `unread` flag, used as the focus acknowledgement check.

All seven assertions passed against the installed **sway version 1.12** using
private sockets under `/tmp`. The test also passed through `make test-runtime`.
Real pointer clicks/hit geometry, typing input/desk rendering, physical displays,
multiple outputs, XWayland, title updates and compositor-process restart remain
uncovered. Sway works with `compositor_experimental=0`; toggling the flag and
reloading preserves its subscription and handles new focus events. This does
not claim recovery from restarting Sway and its Wayland server.
`compositor_experimental` still defaults to zero and controls only Hyprland. See
[the original report](performance/sway-headless-report.md) and the
[Sway promotion report](performance/sway-stable-and-test-warnings-report.md)
for actual results and local verification limits.
