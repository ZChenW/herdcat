# Compositor focus backends

niri remains the default supported backend. `NIRI_SOCKET` takes precedence over
`HYPRLAND_INSTANCE_SIGNATURE`, then `SWAYSOCK`. An advertised experimental
compositor is unavailable unless `compositor_experimental=1` is set globally;
no focus subprocess or event connection is attempted for it by default. No
advertised compositor leaves signs available, but focus, typing-desk tracking
and read acknowledgements unavailable.

Hyprland: **未在真实环境验证** (unverified on real compositors). Sway:
**已在无头 Sway 1.12 上验证** (2026-10-07, headless/pixman).
All seven real-compositor runtime assertions passed. Both experimental backends
remain opt-in. Unit fixtures additionally check payloads, argv and gating.
niri CLI arguments, EventStream parsing,
terminal ancestry/selection and all previous niri test expectations are retained.

| Backend | Initial windows | Events | Window focus |
|---|---|---|---|
| niri | `niri msg -j windows` for clicks; EventStream initial list for tracking | `"EventStream"` on NIRI_SOCKET | `niri msg action focus-window --id ID` |
| Hyprland (experimental) | `hyprctl -j clients` then `hyprctl -j activewindow` | `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock` | `hyprctl dispatch focuswindow address:0xHEX` |
| Sway (experimental) | IPC GET_TREE; `swaymsg -r -t get_tree` for clicks | IPC SUBSCRIBE `["window"]` | `swaymsg -r '[con_id=ID] focus'` |

Commands use separate argv entries, never a shell. Identifiers become checked
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
of this experimental focus layer. Its implementation, activation policy and
regressions have not changed. Legacy Hyprland socket directories are not probed;
the experimental backend implements the current documented XDG runtime path.

Sources: [Hyprland IPC](https://wiki.hypr.land/IPC/),
[Hyprland hyprctl](https://wiki.hypr.land/Configuring/Using-hyprctl/),
[Sway's shipped IPC manual](https://man.archlinux.org/man/sway-ipc.7.en).
The Hyprland documentation specifies event/payload layouts rather than complete
window-event JSON samples; its test strings instantiate those layouts. The Sway
fixtures project the manual's WINDOW example (id 12, pid 19787, null name) and
GET_TREE example (urxvt id 5, pid 23959), with separate regression variants.

Before promoting either backend, a reviewer with that compositor must explicitly
enable it in an isolated test config and verify initial focus, window lifecycle,
title changes, click focus, unread acknowledgement, typing desk, reconnection,
shutdown and disabled opt-in. Stage 34 exercised a private headless Sway 1.12
instance; the remaining desktop acceptance is listed below.

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

`--status` reports `compositor=Sway (experimental) focus-watch=ready` when the
subscription and initial tree are ready. With Sway selected, `--sessions` adds
`sway-session KEY8 con_id=ID seen=yes|no` lines from the live focus map; zero
means no matching live window. These IDs are never persisted. The existing
session rows retain their `unread` flag, used as the focus acknowledgement check.

All seven assertions passed against the installed **sway version 1.12** using
private sockets under `/tmp`. The test also passed through `make test-runtime`.
Real pointer clicks/hit geometry, typing input/desk rendering, physical displays,
multiple outputs, XWayland, title updates and compositor-process restart remain
uncovered. Reload tests subscription reconnection to the same running Sway;
it does not claim recovery from restarting Sway and its Wayland server.
`compositor_experimental` still defaults to zero. See
[the report](performance/sway-headless-report.md) for actual results and local
verification limits.
