# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Fixed

- **Config** - A line with nothing before the `=` is reported as malformed
  (and refused by `--strict`) instead of being skipped in silence. An
  unknown `layer`, `overlay_position` or `cat_align` is reported with its
  line number like every other bad value.

### Internal

- `make fuzz` runs libFuzzer over the parsers of hook payloads, session
  records, agent logs, the config file and the input helper's arguments;
  `make test` replays the seeds and any input that once crashed.
- Recorded hook sequences from real agents are replayed in `make test`, and
  a weekly workflow warns when an agent has released a version newer than
  the one its hooks were last checked against. `make agents-update` then
  upgrades Claude Code, Codex, Copilot CLI and Pi, drives each through a
  short scripted session, and accepts the new recording when the fields
  herdcat reads are unchanged and the sign follows the script.
- `make coverage` reports line and branch coverage of the unit tests
  (73% and 63%); `make coverage-runtime` adds the runtime tests (87% and
  73%).

## [0.4.0] - 2026-10-08

### Added

- **Qwen Code and Antigravity CLI** - Two more agents, connected by `herdcat
  setup` like the others. Both show working, waiting for approval, done and
  error, and lower the sign at once when a turn is cancelled. Antigravity CLI
  (`agy`, the successor of Gemini CLI) has no hook for approval or cancel;
  herdcat reads those from the log agy keeps for itself, and the first prompt
  stands in for the title agy does not give a session.
- **Sway** - Clicking a sign, focus tracking and the typing desk work on Sway
  without the experimental switch. The backend is checked against a real
  headless Sway 1.12 in CI. Not covered there: real pointer clicks, several
  outputs and XWayland windows. `compositor_experimental` remains, for
  Hyprland alone.
- `herdcat --status` ends with a line naming the installed agents whose
  integration is missing or was written by an older version.

### Changed

- **Less memory** - The transparent surface follows what is on screen instead
  of always holding room for `sign_max` boards. With a 110 pixel cat on a 2x
  output the two buffers take 0.9 MB with no signs (8.2 MB before), 1.5 MB
  with up to five closed fan signs and 3.2 MB with a ten-row signpost (12.4 MB
  before). Hovering, a name that must stay visible, the switch card, the font
  panel or a drag ask for the larger surface first, so names appear a few
  milliseconds later; it shrinks again ten seconds after it is no longer
  needed. The cat and the typing desk keep their place throughout.
- **Less CPU while waiting** - A sign that waits for approval repaints only
  the parts that move. One waiting sign on a 2x output costs about 1.0% of a
  core as a fan and 0.8% as a signpost, down from 1.3%. The wobble looks the
  same, frame for frame.
- **Flipping** - Dragging the cat upward turns the signs below it only as near
  the top as the signs themselves need: 114 pixels for a fan of up to five
  with a 110 pixel cat, where it used to be 273. When the switch card does
  not fit above the cat it opens below, and the signs stay up.
- Under Sway and Hyprland, `herdcat --sessions` adds a `window-session` line
  per session with the window it was matched to.
- The README animations show the current signs.

### Fixed

- **Cancelled prompts** - A prompt cancelled before Claude Code, Grok or
  Copilot CLI starts to answer leaves no trace: no hook fires and nothing is
  written, and Copilot says nothing when cancelled in mid-reply either. The
  sign stayed on "working" until the next prompt. herdcat now notices that
  the agent has stopped writing to its terminal and lowers the sign within
  about three seconds, at any point in a turn. The other agents report a
  cancel themselves; all ten were checked.
- **Names after a restart** - A session that herdcat first hears of in the
  middle of a turn, because the cat was restarted or started after the agent,
  showed a placeholder such as `codex e86e` until the next prompt. It now
  gets its project name and title with the first event.
- **Ellipsis** - A shortened name ends in three dots on the baseline, set off
  from the text, in every font. CJK fonts used to centre the dots, level with
  the " · " separator beside them.
- **Hyprland** - Clicking a sign focuses its window on Hyprland 0.56 with a
  Lua configuration, which rejects the older `hyprctl dispatch` form. Found
  by running the backend against a real Hyprland for the first time; it stays
  experimental.
- The font panel no longer drifts by a pixel or two while the switch card
  retracts.

### Internal

- `make lint` passes without warnings and fails on any; functions are held to
  200 lines and 100 statements. The test sources build with `-Werror`.
- `make test` and `make test-runtime` run their tests in parallel
  (`TEST_JOBS`), each under the 1024 open-file limit of an ordinary shell.
  The runtime tests take about six and a half minutes instead of fourteen.
- The Arch `-git` package takes its version from the release tags.

## [0.3.0] - 2026-10-07

### Added

- **No input group** - The keyboard is read by `herdcat-input`, a small
  libc-only helper installed setgid `input`. Users no longer join the `input`
  group, which let every process they run read the keyboard. The helper clears
  its environment, opens only verified `/dev/input/event*` devices, drops the
  group again, filters its system calls and emits nothing but paw movements.
  The previous in-process helper remains as a fallback. See `docs/security.md`.
- **Waiting on subagents** - A session that is idle while agents it started
  are still working is shown as working, with "Waiting on subagent N min", and
  stays up. A small count badge on the fan plate says how many child agents
  are active.
- **systemd user service** - `systemctl --user enable --now herdcat`.
- **Shell completions** for bash, zsh and fish.

### Changed

- **Signpost rows** are two parts: the same state plate the fan uses, next to
  the pole, and a neutral name pill beside it. Collapsed rows show only the
  plate.
- The setup helper module is installed under `share/herdcat`, not `bin`.

## [0.2.0] - 2026-10-06

### Added

- **`herdcat setup`** - Connects the installed agents, tmux and kitty with one
  command. Shows the changes first, backs up every file, can be repeated and
  undone with `--remove`. Needs Python 3.
- **Session titles** - A sign shows its project and the title the agent gave
  the session; until there is one, the first prompt stands in, also for a
  session that began before the cat did. `sign_name`, `sign_name_extra`,
  `sign_title_length` and a free `sign_nameplate` template choose what the
  nameplate says.
- **Ten signs in two rows** - `sign_max` goes to 10, the new default. Up to
  five signs look as before; the rest stand in a back row on longer sticks,
  always drawn behind the front row. Sessions that need attention take the
  front row. The signpost continues to ten rows.
- **Agents started by agents** - An agent launched by another agent joins its
  sign instead of raising its own: the label reads `Claude + Codex`, signpost
  boards show `Claude +2`. Detached children of Claude Code are recognised
  too. They stay visible in `herdcat --sessions` with `parent=`.
- **Dark theme** - `sign_theme=light|dark|auto`, also on a new row of the
  switch card. `auto` follows the desktop's colour scheme through the XDG
  settings portal.
- **More terminals** - Clicking a sign reaches the pane in tmux and WezTerm,
  not only in kitty, and picks the right window in Ghostty and other
  terminals that run several windows in one process. A session in a detached
  tmux is dimmed and says so.
- **Signs below the cat** - The cat can be dragged to the top of the screen;
  with no room above, the signs hang below it.
- **Typing desk distance** - `sign_desk_offset` moves the desk away from the
  cat or closer.
- **Compositor interface** - niri sits behind a small interface. Hyprland and
  Sway backends exist but are experimental, off by default and untested on a
  real compositor (`compositor_experimental=1`).

### Changed

- **Label centring** - Text is centred on the font's cap height, so CJK and
  serif faces no longer sit below the middle of pills and tracks.
- **Signpost boards** fit their content, between 150 and 340 pixels wide at
  the design size, instead of a fixed width.
- **Names** - The default nameplate is the project followed by the session
  title. A sign's name follows the agent only into subdirectories of where
  the session started, so it no longer flips back when the agent steps out.
- **Smaller surface** - The overlay is only as wide as the cat and its signs
  instead of the whole output: about 2 MB of buffers instead of 6 MB on a
  2560 pixel output.
- The moon on the theme row is filled, like the other icons.

### Fixed

- **Hover loop on the signpost** - A board slid out from under a pointer
  resting on its pole-side edge, then back, forever, redrawing at full rate.
- **Font panel memory** - Closing the panel releases the faces it loaded for
  previews. A cat that had shown it once stayed near 45 MB instead of 10.
- The typing desk keeps clear of signs that hang below the cat.

## [0.1.0] - 2026-10-05

First release of herdcat, based on wayland-bongocat 2.0.2.

### Added

- **Session signs** - One sign per coding agent session with its name, agent
  and state, in a fan or a signpost style. Click a sign to jump to its
  terminal, including a single kitty split.
- **Five states** - Working, waiting for approval, done, stopped on error and
  idle. Done and error signs stay up, with a dot, until they have been seen.
- **Eight agents** - Claude Code, Codex, Grok, Kimi Code, Cursor Agent, Copilot
  CLI, Pi and opencode, through `herdcat --hook <agent>` and two bridges.
- **Typing desk** - The sign of the terminal being typed in comes down under
  the paws.
- **Switch card and font panel** - Right-click to change style, language and
  font, with a live preview on the real signs.
- **Dragging** - Move the cat with the pointer; the position is remembered.
- **Silent state changes** - Things agents do not report are inferred: Esc
  before Claude Code starts to answer (from the terminal title), an answered
  question (from a key press, confirmed by the next event), interrupted and
  failed turns (from the session record), and a Codex session run by its
  background server (matched to its terminal by working directory).
- **One sign per process** - Worker threads and changing session ids of one
  agent process share a sign. Sessions with no process, such as opencode,
  fold their unread sign after `agent_stale_timeout`.
- **Keyboard permission errors** - `--status` says `input=denied` with a count
  and explains the fix when no keyboard can be opened.

### Changed

- The command, config directory, state files and socket are named `herdcat`.
- Rendering repaints only what changed and caches the frames of looping
  animations. A waiting sign costs about 2% of one core instead of 18%; an
  idle cat does not wake up at all.

Entries below are the history of wayland-bongocat.

## [2.0.2] - 2026-07-13

### Fixed

- **Fullscreen focus tracking** - The cat becomes visible again after leaving a
  fullscreen window for another window or workspace.

## [2.0.1] - 2026-07-13

### Fixed

- **Concurrent paw animation** - Pressing keys on both sides of the keyboard now
  moves both paws at once (the `both-down` frame). Previously only the
  last-pressed key's paw animated. Per-paw deadlines preserve both input events.
  Fixes #78.
- **HiDPI output placement** - Logical output dimensions, fractional scales,
  transformed outputs, and negative offsets now place overlays correctly. Fixes #75.
- **Layer and hot reload safety** - All layer-shell layers can be configured and
  reloaded safely. Fixes #76.
- **Input and process lifecycle** - Input monitoring restarts after relevant
  configuration changes; child process groups now stop reliably.

### Changed

- **Configuration validation** - Invalid time, boolean, and integer values are
  rejected before they reach runtime.

## [2.0.0] - 2026-04-05

### Breaking Changes

- **PNG → SVG asset migration** - All cat assets are now SVG, rendered via nanosvg. The new artwork has a different aspect ratio (500x277 vs the old 864x360 PNGs, ~1.8:1 vs ~2.4:1). **Users must re-tune their config values** — at the same `cat_height`, the cat will appear narrower than before. Recommended starting point: `cat_height=110`, `overlay_height=120`.
- **`enable_antialiasing`** is now a deprecated no-op. SVG rendering always produces anti-aliased output. The option is accepted but ignored.

### Added

- **SVG Rendering** - Pixel-perfect rendering at any size via nanosvg. No more scaling artifacts or blurry edges.
- **Sleeping Animation** - 5th animation frame. `idle_sleep_timeout` now shows a sleeping cat instead of hiding the overlay.
- **Fast Input Retry** - Input child retries device scanning every 5 seconds until devices are found, then switches to the configured `hotplug_scan_interval`. Fixes #69.

### Changed

- **Default `hotplug_scan_interval`** reduced from 300s to 30s for faster device detection after boot.
- **Build system** - Generated Wayland protocol files are now committed to git. Building from source no longer requires `wayland-scanner` or `wayland-protocols`. Use `make protocols` to regenerate after updating XML sources. `make clean` no longer removes protocol files; use `make distclean` for that.
- **Nix derivation** - Dropped `wayland-scanner` and `wayland-protocols` build dependencies since protocol bindings are pre-generated.

### Fixed

- **Hot-reload crash** (#71) - Changing `overlay_height`, `overlay_position`, or `layer` while running with `-w` no longer crashes. Uses wlr-layer-shell double-buffered properties instead of destroying/recreating surfaces.
- **Build failure** (#73) - Required protocol bindings are committed to git,
  eliminating build-time `wayland-scanner` dependencies.
- **KWin fullscreen detection** (#28) - Relaxed the global fullscreen fallback for compositors (KDE/KWin) that don't send per-toplevel output events. Overlay now auto-hides during fullscreen on KDE.
- **Missing protocol error** (#70) - Error message now names the specific missing protocol instead of a generic failure.
- **SVG edge artifacts** - Premultiplied alpha compositing for correct Wayland ARGB8888 rendering, eliminating dark fringe on anti-aliased edges.

---

## [1.4.0] - 2026-02-14

### Added

- **Multi-monitor CSV** - `monitor` now accepts comma-separated output names; parent process launches dedicated children via `--multi-monitor-child` and CLI docs reflect new flow.
- **Safe hot reload hook** - Config watcher now only signals reloads, the actual reload runs from a Wayland tick callback so Wayland structures stay main-thread bound.

### Changed

- **Config model** - `config_t` now stores per-config device/output arrays, inline comments are stripped, and `monitor` parsing loads multiple names with fallback to automatic output; keyboards hotplug state is rebuilt cleanly on reload.
- **Wayland rendering** - Draw path takes `anim_lock`, surface recreation checks the applied layer/output/size snapshots, and buffer recreation is serialized to avoid tearing/races when configs change.
- **Documentation & packaging** - README/`bongocat.conf.example` demonstrate comma-separated monitors, Makefile installs now ship the example config, and CLI help/version text was refreshed for v1.4.0.

### Fixed

- **Dangling pointers on reload** - `g_config` swap, input device lists, and watcher cleanup now occur only after new config is validated so the old config isn’t left referencing freed memory.
- **Config watcher and animation races** - Atomic flags and mutex coverage were tightened, `config_reload_callback` no longer touches Wayland structures directly, and animation draw/update now handle surface recreation safely.
- **Input hotplug robustness** - Path truncation is guarded, static device comparisons check nulls, and signal handlers close cleanly when the child early-exits.

---

## [1.3.2] - 2025-12-07

### Fixed

- **Monitor Reconnection** - Overlay now survives monitor disconnect/reconnect (fixes #15)
- **Dynamic Overlay Resize** - Changing `overlay_height` via config reload no longer crashes
- **Ghost Process Prevention** - Added SIGQUIT/SIGHUP handlers and parent liveness check in child

### Improved

- **Performance Optimizations**
  - Fast buffer clearing using memset (~4x faster)
  - Skip unchanged frames when idle (~95% fewer redraws)
  - Hoisted loop invariants in image scaling
- **Thread Safety** - Mutex protection during buffer recreation
- **Memory Usage** - Reduced from ~20MB to ~8MB RAM

---

## [1.3.1] - 2025-12-06

### Added

- **Keyboard Hand Mapping** - Left half of keyboard triggers left cat hand, right half triggers right hand
- New config option `enable_hand_mapping=1` (enabled by default)
- Hand mapping respects `mirror_x` - hands flip when cat is mirrored

### Changed

- `enable_hand_mapping` default is now `1` (enabled)
- NixOS module `enableHandMapping` default is now `true`

---

## [1.3.0] - 2025-12-06

### Added

- **Improved Anti-Aliasing** - Box filter for downscaling + proper alpha blending for smooth edges at any size
- **Interactive Keyboard Detection** - New `--interactive` mode in `bongocat-find-devices` listens for actual key presses
- **Hot-Reload Device Changes** - Changing keyboard devices in config now works without restart
- **C23 Modern Codebase** - RAII macros, `[[nodiscard]]` attributes, guard clauses throughout

### Fixed

- **Hot-Reload Bug** - Keyboard device path changes now properly trigger input restart
- **Memory Leaks** - Fixed realloc leaks in config parsing
- **Thread Safety** - Atomic operations for shared state between threads
- **Use-After-Free** - Fixed crash in config reload callback
- **Fullscreen Detection** - Now correctly tracks active window per workspace

### Improved

- **README** - Streamlined documentation with minimal config example
- **Script Reliability** - Device detection script distinguishes actual keyboards from power buttons/hotkeys
- **Code Quality** - Organized headers, consistent naming, comprehensive error handling

## [1.2.5] - 2025-08-26

### Added

- **Enhanced Configuration System** - New config variables for fine-tuning appearance and behavior
- **Sleep Mode** - Scheduled or idle-based sleep mode with customizable timing

### Fixed

- **Fixed Positioning** - Fine-tune position, defaults to center

### Improved

- **Default Values** - Refined default configuration values for better out-of-box experience

## [1.2.4] - 2025-08-08

### Added

- **Multi-Monitor Support** - Choose which monitor to display bongocat on using the `monitor` configuration option
- **Monitor Detection** - Automatic detection of available monitors with fallback to first monitor if specified monitor not found
- **XDG Output Protocol** - Proper Wayland protocol implementation for monitor identification

### Fixed

- **Memory Leaks** - Fixed memory leak in monitor configuration cleanup
- **Process Cleanup** - Resolved child process cleanup warnings during shutdown
- **Segmentation Fault** - Fixed crash during application exit related to Wayland resource cleanup

### Improved

- **Error Handling** - Better error messages when specified monitor is not found
- **Resource Management** - Improved cleanup order for Wayland resources
- **Logging** - Enhanced debug logging for monitor detection and selection

## [1.2.3] - 2025-08-02

### Added

- **Smart Fullscreen Detection** - Automatically hides overlay during fullscreen applications for a cleaner experience
- **Enhanced Artwork** - Custom-drawn bongocat image files by [@Shreyabardia](https://github.com/Shreyabardia)
- **Modular Architecture** - Reorganized codebase into logical modules for better maintainability

### Improved

- **Signal Handling** - Fixed duplicate log messages during shutdown
- **Code Organization** - Separated concerns into core, graphics, platform, config, and utils modules
- **Build System** - Updated to support new modular structure

## [1.2.2] - Previous Release

### Added

- Automatic screen detection for all sizes and orientations
- Enhanced performance optimizations

## [1.2.1] - Previous Release

### Added

- Configuration hot-reload system
- Dynamic device detection

## [1.2.0] - Previous Release

### Added

- Hot-reload configuration support
- Dynamic Bluetooth/USB keyboard detection
- Performance optimizations with adaptive monitoring
- Batch processing for improved efficiency

## [1.1.x] - Previous Releases

### Added

- Multi-device support
- Embedded assets
- Cross-platform compatibility (x86_64 and ARM64)
- Basic configuration
