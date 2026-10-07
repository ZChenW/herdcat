# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

- Idle and read done parents with active subagents now display and rank as
  working, with “Waiting on subagent N min” / “等待子代理 N 分钟” measured
  from the oldest still-active child's start. Actual session state and alerts
  are unchanged. Fan plates show an upright active-child badge, capped at `9+`,
  switching to the left corner beside unread completion; post boards use `+N`.

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
