# Compositor focus backends

niri remains the default supported backend. `NIRI_SOCKET` takes precedence over
`HYPRLAND_INSTANCE_SIGNATURE`, then `SWAYSOCK`. An advertised experimental
compositor is unavailable unless `compositor_experimental=1` is set globally;
no focus subprocess or event connection is attempted for it by default. No
advertised compositor leaves signs available, but focus, typing-desk tracking
and read acknowledgements unavailable.

Hyprland and Sway: **未在真实环境验证** (unverified on real compositors). Unit
fixtures check documented payloads, argv and opt-in gating only. They are not a
claim of working desktop integration. niri CLI arguments, EventStream parsing,
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
shutdown and disabled opt-in. This implementation session did not start or
connect to any real compositor socket.
