# Architecture

The renderer owns one Wayland connection and one event loop for every overlay.
One input helper is executed with `posix_spawn()` through `/proc/self/exe`.
There are no animation or configuration-watcher threads and no per-monitor
processes. Runtime dependencies are C23, Linux evdev, Wayland client, FreeType and
Fontconfig. The text libraries are an intentional dependency: the signs need real text.

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
and runs their shared event loop.
`platform/overlay_present.c` keeps damage, double-buffer selection, pixel
painting, input regions, frame callbacks and surface commits together.
`platform/overlay_pointer.c` holds pointer/seat callbacks, drag/click/cursor
state and the cleanup used on pointer loss and overlay teardown.
`src/platform/overlay_internal.h` shares existing overlay state and declarations
privately between lifecycle, presentation and pointer handling.
`platform/shm_buffer.c` owns buffer allocations and release lifetime. Automatic
selection uses one available output; explicit missing outputs wait while other
overlays continue. Configuration reload reconciles
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
requested. Pointer enter selects an overlay. When configured, visible and
draggable, its input region is the cat rectangle plus visible sign plates and
the expanded hover pad; otherwise it is empty. Off uses the cat rectangle only.
Region and pixels are committed together after configure, scale, reload,
visibility/fullscreen and horizontal-position changes. Pointer capabilities,
seat removal and overlay teardown share cleanup paths. Optional cursor-shape
objects follow the pointer lifetime and add no library dependency.

Transparent overlays use a content-width surface anchored at the left edge.
`platform/overlay_geometry.c` owns the pure extent and output-to-surface
placement functions. Persisted x remains an output logical coordinate; the
cat's surface coordinate is that x minus the left margin. At output edges,
the surface stops moving and the cat moves inside it. Origins align to the
physical pixel grid for fractional scales. Nonzero overlay opacity retains
the output-wide background bar.

`platform/overlay_vertical.c` selects above/below signs from the cat's output
height, with a 24px return hysteresis. `platform/overlay_position.c` maps the
saved displacement to the surface margins and per-surface cat coordinates.
Transparent sign overlays clamp the cat rather than the whole surface, and
keep the surface inside the output by changing its origin at either edge.
The desk's lift is limited by available space at the top. Background bars and
signs-off retain their old placement. Bottom position records retain their old
anchor-relative displacement; transparent signs at the top use the cat's top
as their saved displacement.

After four logical pixels of left-button motion, both axes follow layer
margins, with at most one combined margin submission per surface frame
callback. A changed surface-local cat coordinate rebuilds pixels and input
in the same commit. During the implicit button
grab the compositor reports coordinates against the surface position at press,
so they do not shift as the margin moves the surface; the margin is computed
absolutely from the press-time margin and grab point (`drag_margin_follow`),
and the callback submits any travel that arrived while it was pending.
Both axes retain the newest pointer travel while waiting for a callback. Release, leave or pointer loss saves the position
and destroys outstanding callbacks. Pause preserves pointer interaction.

`platform/drag.c` owns geometry and the bounded per-output position file in the
XDG state directory. Positions use logical pixels and convert to physical pixels
only for rendering. The directory is mode 0700 and the atomic replacement file
mode 0600; descriptor-based opens reject symlinks and non-regular files. Reload,
scale/output changes and reconnection clamp saved positions. `reset-position`
removes the state file and resets all overlays without rewriting configuration.

`config/config.c` owns loading, diagnostics, strict/tolerant policy, defaults
and monitor overrides without input access.
`config/config_parse.c` holds existing file, section and key/value parsing.
`config/config_validate.c` holds existing range checks and normalization.
`src/config/config_internal.h` shares configuration helpers, ranges and mutable
parse/diagnostic state inside this one configuration module.
Flat files remain supported. `[monitor:NAME]` overrides appearance; `[global]`
returns to global settings. Overrides are applied after all global settings, independent of
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
before error, done, working and idle. It owns all done/stale deadlines and unread completion; animation only
receives the resolved state and redraws when it changes. With signs off, display priority is scheduled sleep, held paws, resolved agent
artwork, idle sleep, then idle frame. Enabled signs suppress whole-cat agent
artwork, retaining paw and sleep animation.
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

Child ownership uses the top session's creation order, surviving provisional
key adoption. The renderer reads at most 32 `/proc/<pid>/stat` parent links
on first registration and unmerged metadata handoffs. Headless hooks append
an actual-PID candidate and metadata flag to the existing `ev` request; the
authenticated transport and its buffer stay unchanged. Confirmed children
keep their own process watches and table rows but are excluded from state
resolution, sign selection, focus acknowledgement, titles and typing desks.
Creation-ordered snapshots aggregate only working/waiting child types for
fan templates and a total count for post labels. Child completion timers do
not wait for acknowledgement; children retain their hidden association when
the parent row is removed until their own END, exit or existing timeout.
No thread, full process scan, periodic ancestry refresh or new poll fd is added.

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
compiler-generated header dependencies. `build/herdcat` selects the build.
`make test` runs deterministic regression suites; `make test-runtime` uses a
small Wayland server fixture (test-only libwayland-server) for multiple outputs,
scale/resolution changes, disconnect/reconnect, release and queue pressure.
`make test-sanitize` checks unit suites with ASan/UBSan; `make debug` also
instruments the real runtime. Release retains PIE, RELRO and stack hardening.

## Session window focus

`platform/focus.c` owns one asynchronous terminal/focus CLI job at a time. Window query
and focus share a one-second deadline, bounded output, checked exit status,
and cleanup/reaping. Its descriptor and deadline join the renderer poll loop.
`compositor_niri_windows.c` extracts niri window IDs/PIDs without interpreting titles as fields; `focus_json.c` parses WezTerm replies.
The nearest matching process ancestor (up to 16) supplies the terminal window.
`--focus` accepts a full session key or a unique eight-character prefix; a
successful command response means queued, with the job result consumed later.
The default backend is niri. Experimental Hyprland/Sway backends are opt-in.
No shell commands are constructed.

## Session sign rendering and focus tracking

`graphics/signs.c` is a pure model: supplied time, selected session views,
configuration and pointer state produce shared shape/text/hit lists for fan and
post.
`graphics/signs_transform.c` reflects a frame about the cat's horizontal
center before the typing desk is emitted. Text and grouped icons stay upright;
reflected bases rasterize from their original side and flip their bitmap to
preserve NanoSVG edge sampling. Orientation changes reset sign transitions
while retaining the menu and desk scalar state.
`graphics/signs_fan.c` holds fan orbit and plate layout. Above five visible
sessions it assigns the priority front row and emits complete back groups
before front groups, with hover sorting inside each row. Retained slots animate
row changes. The pure `signs_hit` shares the rotated plate geometry and front
precedence with overlay pointer handling; single-row hit behavior is unchanged.
`graphics/signs_post.c` holds post-board layout, while pole/slot assembly stays
inside the shared `signs_frame` function.
`graphics/signs_menu.c` holds the switch-card geometry and glyph-layout functions.
`src/graphics/signs_internal.h` shares layout types, constants and declarations
privately without extending the model's public interface.
`graphics/sign_draw.c` rasterizes NanoSVG shapes into premultiplied BGRA
with a bitmap LRU bounded by 2048 slots and 16 MiB of pixels, including settled
nameplate backgrounds. Full-motion waiting shapes align bitmap origins to
physical pixels. After 60 seconds without any waiting session, a one-shot
deadline releases waiting phase bitmaps, retaining those also used by ordinary
shapes. On glibc, this also returns freed heap pages to the kernel. The core
session tick cancels that deadline when waiting resumes.
`graphics/text.c` uses FreeType grayscale glyphs,
a 512-entry glyph LRU, bounded faces and Fontconfig per-codepoint fallback.
Scale changes clear glyph/bitmap caches; a font reload clears both. There is no
HarfBuzz shaping (no RTL, ligatures or combining-character layout).

`platform/overlay_signs.c` owns per-output models, hover and click state, the
150 ms close delay, surface clearance, damage unions and typing desk. Wayland
only consumes its frame, input rectangles and next wake time. Transitions ask
for frame callbacks; settled working dots wake every 180 ms and waiting loops
at 48 phase boundaries per 1500 ms cycle (31.25 ms per phase), sharing the
same 25 positions on the return trip. Reduced disables loops; off makes
transitions instantaneous.
Visible elapsed-minute labels retain a minute deadline. A full-motion desk
caret has a 500 ms deadline only while visible. No sessions, hover or typing
means no sign-related periodic wakes. Hidden overlays schedule no sign frames.
Existing input hotplug scanning is independent and can be disabled with
hotplug_scan_interval=0 when measuring total idle wakes.

Sign configuration is global. Reload selects fan/post/off and rebuilds surface
geometry through normal reconciliation. It retains sessions, applies the idle
policy and limit, changes font/language/animation immediately, and releases
existing unread completions onto timers when switching sticky to timeout.
The old HERDCAT_SIGN_STYLE environment override is removed. Off restores the
original cat geometry and agent frames; completion policy remains independent.

`platform/compositor_niri.c` reads niri's asynchronous EventStream into a bounded
window/PID map owned by `focus_watch.c`, including initial is_focused state. Its fd joins agent_watch's
epoll, preserving six basic fds plus the control socket in the seven-slot poll
budget. `focus_current_query` resolves ancestry once per session for both the
seen and chosen results; no ancestry survives the query, so reparenting and
window/PID changes are observed on the next query.
Connection loss backs off; absent niri disables the desk and treats completions
as unread. Successful focus observations (or sign clicks) acknowledge unread
completion. Submissions and session removal also clear unread.

Paw activity plus a focused session enables a 2.5-second typing desk, retaining
that session's sign slot. Focus loss, working submission, disabling the option
or hiding dismisses it. The board never joins the pointer input region. Only
presence of paw activity is used, never key contents. The confirmed desk top is
68 design pixels; drag margin calculation remains absolute from button press.

## Switch card, font panel and stored choices

`graphics/signs.c` also lays out the switch card (style, language, font, theme rows)
with the sign model's shapes, texts and hit rectangles.
`graphics/sign_palette.c` supplies the shared light/dark palette to signs and
the font panel. Theme travels with each frame input and panel model; bitmap
keys already include colours, so switching needs no cache reset.
`platform/overlay_signs_geometry.c` holds surface clearance, resting placement
and the per-output placement handoff to the sign model.
`platform/overlay_menu.c` holds existing menu choices, preview restoration, save
and close deadlines, font-panel anchors and switch-card button/scroll handling.
`src/platform/overlay_signs_internal.h` privately shares the existing per-output
lanes and interaction state needed to preserve menu/frame ordering.
`platform/overlay_signs.c` keeps per-output frames, damage, regions and typing
desk; its existing step still orders menu updates, frame construction and panel
synchronization without changing behavior.
The 800 ms leave and 6 s idle timers, paw tap and half-second font-save debounce
remain unchanged.

`graphics/font_panel.c` is the panel's pure model and drawing: two columns,
ten visible rows, a three-way filter, hover and selection. Every box has a
fixed size; only glyphs change with the face. `platform/font_panel_catalog.c` holds the unchanged catalog and recent-family
storage functions behind `src/platform/font_panel_internal.h`.
`platform/font_panel.c` owns a
separate layer surface that exists only while the panel is open, its two
buffers, pointer handling and placement beside the card inside the output.
The always-present overlay surface is not made taller for it. Catalog order
is rebuilt each time the panel opens, with the four recent families first,
and never while it is open so cells do not move under the pointer.

While the panel is open (browsing) the overlay hides the card, raises every
sign, and applies the hovered family as the main face without saving. The
chosen family is held separately and restored when the pointer leaves the
cell. The panel draws its own words in the chosen family for the same reason.
The leave timer does not start until the pointer has reached the panel, since
the card is no longer under it. When the panel closes the menu closes.

`graphics/text.c` remembers which face draws each (family, code point,
weight) and asks a family's own face before asking Fontconfig. Without this
every glyph cost a Fontconfig lookup on every draw: a page of font names took
about 940 ms per redraw and a CJK label about 18 ms per frame. Eviction and panel cleanup compact face indexes in both route and glyph
caches. Used main-family fallbacks have a separate route table so browsing
families cannot overwrite their routes. Panel close releases browsing faces
and panel-only shape bitmaps, retaining the chosen family's used resources.
Hover holds those resources until the menu restores or accepts a family;
close-time heap trimming waits for that restoration when necessary. The
Fontconfig enumeration result is destroyed immediately after copying names
into the fixed-size catalog, which remains available for the next opening.

`platform/prefs.c` stores menu choices as tab-separated
`key, choice, config value at the time` lines and still reads the earlier
space-separated form. A record applies only while the config value matches
its third field. `fonts-recent` is a plain list beside it. Both are written
through a temporary file and rename, mode 0600, opened with O_NOFOLLOW.

## Transcript interruption detection

`core/agent_adapters.c` declares interrupt/error sources (hook, transcript or
none). Claude uses transcript interruptions; Codex uses Interrupt hooks plus
transcript abort/error fallback. `core/agent_hook.c` forwards only metadata-event
paths in `path <key16hex> <absolute-path>` requests, after JSON unescaping with a
1024-byte decoded limit. The authenticated control receive buffer is 1280 bytes;
its existing length guard rejects packets of 1280 bytes or more.

`platform/transcript_watch.c` owns fixed session slots, recording descriptors,
one nonblocking inotify and a continuation eventfd. Both event fds join
agent_watch's existing epoll alongside focus_watch, with main dispatching tokens;
the six basic fds and seven-slot external budget stay unchanged. Submissions
reset offsets to EOF. Only working/waiting sessions retain watches, and ending,
evicting, disabling or completing sessions releases them. Re-enabling starts at
EOF. No timer, worker thread or periodic wake is added. Each wake reads at most
256 KiB total, with round-robin continuations through eventfd only while unread
bytes remain. The last disarm closes both notification fds.

Path opening walks each component from / using openat/O_NOFOLLOW, rejects ..,
confines the path to HOME, and checks regular-file type and UID through fstat.
Inotify attaches to the opened inode via /proc/self/fd. Replacement/truncation or
read errors fail closed until a new path handoff, leaving timeout fallback.

`core/agent_transcript.c` validates each bounded complete JSON line, then examines
structural slices without a DOM or allocation. Claude requires top-level user,
message.role=user and a single text block with the interruption prefix. A
one-second post-submission guard reduces literal-prompt false positives. Codex
requires event_msg with turn_aborted or task_complete plus an error object;
normal completion is ignored. The idempotent session interrupt operation only
changes working/waiting to idle, preserving unread done and configured timers.
Neither paths nor transcript contents/error messages are printed by the monitor;
contents are never persisted or transmitted. These private formats may change:
unsupported input silently falls back to existing stale deadlines. No new
runtime dependency is required.


## Terminal locations

Session terminal records keep kitty in the first slot, with its existing socket,
pid and split semantics. The second slot identifies tmux, WezTerm or Ghostty.
Hook metadata hands off a bounded `term` message with a hex-encoded socket;
restoration/discovery can read the same environment from /proc. Decimal pane
identifiers and absolute, user-owned socket paths are checked before use.
Terminal records and the at-most-96-byte decoded window titles are never saved
or logged. The authenticated control buffer remains unchanged.

`platform/focus.c` serializes terminal discovery, focus and on-demand current-pane
queries through the existing bounded `job_start` and its poll descriptor. Each
external command uses argv and a one-second timeout, without a shell. WezTerm's
socket is passed in a private child environment. tmux resolves the latest
attached client, then focuses niri, optionally the client's kitty split, and
finally switches the tmux client. WezTerm activates its pane, queries its title,
then focuses niri. Ghostty selects a unique repository-name title match or the
original first candidate. Window title ranking is pure and shared with current
session matching.

WezTerm current-pane requests come only from a system-window focus change or
input needing to distinguish several sessions, with per-window 500 ms dedupe.
A focus event preceding terminal registration retains its request until the
socket metadata arrives. New panes can reuse an observed mux-window group.
While a query is pending or unusable, no WezTerm pane is considered seen. Recent
input feedback waits for the response; no new timer or thread is introduced.
Mux window identities learned from list/current replies associate inactive panes
with the same system window. tmux reports carry the validated server socket in a separate pane namespace,
permitting pane zero and retaining kitty's original nonzero report rules and wire
form. Every query resolves reported client ancestry against the current niri
windows; multiple attached clients and identical pane IDs on different servers
remain distinct.
Title-based cancellation is disabled for tmux.

## System theme and compositor backends

`platform/theme_watch.c` starts busctl only for the resolved preference auto.
ReadOne (with Read fallback) has the shared one-second command-job deadline;
a separate monitor pipe supplies bounded Settings signal lines. Both fds join
agent_watch epoll. Failed jobs/listeners are reaped and reconnect with backoff;
explicit light/dark terminate them. `sign_theme_effective` resolves the palette
while the card retains its automatic selection. Missing busctl uses light.

`platform/compositor.c` selects niri, Hyprland or Sway in environment order;
the latter two require compositor_experimental. The ops table supplies stream
lifecycle, window parsing and argv construction. `compositor_niri_json.c` and
`compositor_niri_windows.c` retain the original niri parsers; the shared map and
session matching remain in focus_watch. `compositor_stream.c` supplies bounded
nonblocking experimental transports: Hyprland socket2 plus event-triggered
client/activewindow jobs, or Sway native-endian i3 IPC with subscription/tree
requests. These backends are unverified on real compositors. The existing
hyprland.c fullscreen fallback stays independent and unchanged.

`core/runtime_sessions.c` holds the moved session/terminal/focus coordination;
its private runtime_internal.h shares existing application state with main.
`platform/command_job.c` holds the moved spawn/deadline/reaping mechanism.
Theme and experimental snapshot descriptors use the existing agent epoll;
six basics plus the control socket still fit the seven external poll slots.
