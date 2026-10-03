# Architecture

The renderer owns one Wayland connection and one event loop for every overlay.
One input helper is executed with `posix_spawn()` through `/proc/self/exe`.
There are no animation or configuration-watcher threads and no per-monitor
processes. Runtime dependencies remain C23, Linux evdev and Wayland client.

## Ownership and event flow

The renderer polls Wayland, the private input socket, directory inotify,
authenticated control connections, agent process epoll and a signal eventfd. Timeouts are the
nearest animation, debounce, helper-recovery, control, agent session or Hyprland deadline.
Idle overlays have no animation timeout; sleeping overlays wait for meaningful
deadlines. Animation redraws respect live FPS changes, including `fps=1`.
Wayland read preparation is cancelled whenever a poll wakeup has no display
input. Flush backpressure adds POLLOUT interest.

Each overlay owns a layer surface, optional viewport/fractional-scale object,
two release-aware SHM buffers, effective monitor configuration, animation
state and frame cache. Surface reconstruction and teardown share one path.
Busy retired buffers remain mapped until release. Frame caches rebuild only
when dimensions, scale, mirroring or antialiasing parameters change. Buffer,
stride, scaling, placement and clipping arithmetic uses checked wide values.

`platform/outputs.c` owns stable output slots and protocol metadata.
`platform/wayland.c` reconciles selections, creates and tears down overlays,
and renders them. `platform/shm_buffer.c` owns buffer allocations and release
lifetime. Automatic selection uses one available output; explicit missing
outputs wait while other overlays continue. Configuration reload reconciles
selection without restarting the process. Configure events determine actual
surface dimensions.

`graphics/animation.c` shares parsed embedded SVGs and the rasterizer, while
holding paw deadlines and caches separately for each overlay. Input packets
contain only paw bits, monotonic timestamps and device counts. Configured
mirroring and hand mapping are applied independently for each overlay.
Pause clears activity, displays the idle frame and discards input; resume
resets deadlines. Hide suppresses visibility without stopping animation.

Fullscreen tracking stores every output occupied by a toplevel. State and
output changes are staged until `done`. Output removal and overlay selection
recompute visibility. The optional Hyprland fallback uses asynchronous,
bounded `posix_spawnp()` jobs: one-second deadline, bounded output, checked
exit status and termination/reaping on failure. It does not block Wayland.

## Configuration and control

The first Wayland seat supplies `wl_pointer` events only; no keyboard seat is
requested. Pointer enter selects an overlay. Its input region is the cat's
logical bounding rectangle when configured, visible and draggable, otherwise
empty. Region and pixels are committed together after configure, scale, reload,
visibility/fullscreen and horizontal-position changes. Pointer capabilities,
seat removal and overlay teardown share cleanup paths. Optional cursor-shape
objects follow the pointer lifetime and add no library dependency.

After four logical pixels of left-button motion, x changes the cat's in-buffer
position and marks a redraw. Y changes the layer margin, with at most one
margin submission per surface frame callback. During the implicit button
grab the compositor reports coordinates against the surface position at press,
so they do not shift as the margin moves the surface; the margin is computed
absolutely from the press-time margin and grab point (`drag_margin_follow`),
and the callback submits any travel that arrived while it was pending.
Horizontal motion continues
while waiting for a callback. Release, leave or pointer loss saves the position
and destroys outstanding callbacks. Pause preserves pointer interaction.

`platform/drag.c` owns geometry and the bounded per-output position file in the
XDG state directory. Positions use logical pixels and convert to physical pixels
only for rendering. The directory is mode 0700 and the atomic replacement file
mode 0600; descriptor-based opens reject symlinks and non-regular files. Reload,
scale/output changes and reconnection clamp saved positions. `reset-position`
removes the state file and resets all overlays without rewriting configuration.

`config/config.c` parses and validates without input access. Flat files remain
supported. `[monitor:NAME]` overrides appearance; `[global]` returns to global
settings. Overrides are applied after all global settings, independent of
section order. Input and timing stay global. Startup remains tolerant;
strict checking and reload reject malformed, unreadable and missing files.
Reload creates a temporary configuration before swapping the active one.

`config/config_watcher.c` watches the parent directory, filters the basename,
and reloads 300 ms after the last relevant event. Atomic replacement,
deletion/recreation, overflow and invalidated directory watches are handled.

`core/control.c` locks a user-owned regular PID file before truncation, rejects
symlinks and unsafe metadata, and retains its inode between runs. The lock is
held until all cleanup finishes. Controls use a mode-0600 Unix sequenced-packet
socket, SO_PEERCRED same-UID authentication, bounded messages and deadlines.
Toggle requests a stop through the socket; it never trusts a stale PID to
signal an unrelated process group.

## Agent sessions and hooks

`core/agent_sessions.c` owns a fixed 32-slot table keyed by the FNV-1a hash of
agent and session ID; key zero is the manual session. The table resolves waiting
before done, working and idle. It owns all done/stale deadlines; animation only
receives the resolved state and redraws when it changes. Display priority is
scheduled sleep, held paws, resolved agent artwork, idle sleep, then idle frame.
Pause and hidden outputs do not suspend session expiry. Reload preserves sessions.

`platform/agent_watch.c` deduplicates process pidfds under one epoll fd. The main
loop removes sessions for exited processes, expires deadlines and resolves state
before processing controls. END, eviction and PID replacement prune watches
only when no remaining session references them. Unavailable pidfds fall back to
stale expiry; watched waiting sessions do not expire. There is no polling worker.

`core/agent_hook.c` streams stdin into a bounded JSON scanner, maps lifecycle
events and sends a short `ev` request through the existing authenticated control
socket. It never emits stdout and has a two-second alarm. The caller discovers
the agent PID by walking at most eight process ancestors, skipping shell wrappers.
`--state` controls the reserved manual session; `--sessions` lists the table.

## Input and privilege boundaries

Dragging adds compositor-delivered `wl_pointer` input in the unprivileged
renderer. Keyboard animation continues to use the existing evdev helper;
dragging never requests `wl_keyboard` or changes device permissions.

The renderer drops real, effective and saved setgid privilege before loading
configuration. Executing the installed binary reacquires its setgid group
only inside helper mode. The helper lowers the effective group while reading
and raises it only for discovery/opening; this also works after helper restart.
No setgid installation or permission grant is performed automatically.

The helper authenticates its inherited socketpair and arms PR_SET_PDEATHSIG
with a parent-race check. `posix_spawn` closes unrelated descriptors. Explicit
paths/names select devices without unrelated fallback. Empty selectors use
EVIOCGBIT keyboard capability queries. Stable aliases are compared by device
identity to avoid duplicates. HUP/ERR/NVAL remove disconnected descriptors;
normal periodic scanning retries connections. Debug never logs keycodes.

SIGTERM, SIGINT, SIGQUIT and SIGHUP wake and stop the renderer. Cleanup stops
input, waits a bounded grace period, escalates to SIGKILL if necessary, reaps
the helper, tears down overlays and finally releases the singleton lock.

## Build and validation

Objects and binaries live under `build/debug` and `build/release`, with
compiler-generated header dependencies. `build/bongocat` selects the build.
`make test` runs deterministic regression suites; `make test-runtime` uses a
small Wayland server fixture (test-only libwayland-server) for multiple outputs,
scale/resolution changes, disconnect/reconnect, release and queue pressure.
`make test-sanitize` checks unit suites with ASan/UBSan; `make debug` also
instruments the real runtime. Release retains PIE, RELRO and stack hardening.
