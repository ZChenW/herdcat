# Bongo Cat Wayland Overlay

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-2.0.2-blue.svg)](https://github.com/saatvik333/wayland-bongocat/releases)

A cute Wayland overlay that shows an animated bongo cat reacting to your keyboard input.

![Demo](assets/demo.gif)

## Features

- 🎯 Real-time keyboard animation
- 🔥 Hot-reload configuration
- 🎮 Auto-hides in fullscreen apps
- 🖥️ Multi-monitor support
- 😴 Idle/scheduled sleep mode
- 🎨 SVG-based rendering (pixel-perfect at any size)
- ⚡ Lightweight (~8MB RAM)

## Quick Start

### Install

```bash
# Arch Linux
yay -S bongocat

# Other distros - build from source
git clone https://github.com/saatvik333/wayland-bongocat.git
cd wayland-bongocat && make
```

### Setup Permissions

```bash
sudo usermod -a -G input $USER
# Log out and back in
```

### Find Your Keyboard

```bash
bongocat-find-devices  # or ./scripts/find_input_devices.sh
```

### Run

```bash
bongocat --watch-config
# Optional: force one monitor from CLI
bongocat --watch-config --monitor eDP-1
```

## Configuration

Create `~/.config/bongocat/bongocat.conf`:

```ini
# ═══════════════════════════════════════════════════════════════════════════
# BONGO CAT CONFIG - Minimal defaults, uncomment to customize
# ═══════════════════════════════════════════════════════════════════════════

# Position & Size
cat_height=110
cat_align=center
# cat_x_offset=0
# cat_y_offset=0

# Appearance
overlay_height=120
overlay_opacity=0
overlay_position=bottom
# mirror_x=0
# mirror_y=0

# Input device (run bongocat-find-devices to find yours)
# Optional: keyboard_device=/dev/input/by-id/YOUR-KEYBOARD-event-kbd

# Multi-monitor (comma-separated monitor names)
# monitor=eDP-1,HDMI-A-1

# Sleep mode (optional)
# idle_sleep_timeout=300
# enable_scheduled_sleep=0
# sleep_begin=22:00
# sleep_end=06:00
```

### Options Reference

<details>
<summary>Click to expand all options</summary>

| Option                     | Values            | Default  | Description                          |
| -------------------------- | ----------------- | -------- | ------------------------------------ |
| `cat_height`               | 10-200            | 40       | Cat size in pixels                   |
| `cat_align`                | left/center/right | center   | Horizontal alignment                 |
| `cat_x_offset`             | any int           | 100      | Horizontal offset from alignment     |
| `cat_y_offset`             | any int           | 10       | Vertical offset from center          |
| `enable_antialiasing`      | 0/1               | 1        | **Deprecated** — no-op with SVG      |
| `overlay_height`           | 20-300            | 50       | Overlay bar height in pixels         |
| `overlay_opacity`          | 0-255             | 150      | Background opacity (0=transparent)   |
| `overlay_position`         | top/bottom        | top      | Screen edge position                 |
| `layer`                    | background/bottom/top/overlay | top | Wayland layer type            |
| `keyboard_device`          | /dev/input/path   | auto     | Specific evdev device to monitor     |
| `keyboard_name`            | string            | —        | Match device by name (for hotplug)   |
| `monitor`                  | comma list        | auto     | Monitors to render on                |
| `fps`                      | 1-120             | 60       | Animation frame rate                 |
| `mirror_x`                 | 0/1               | 0        | Flip cat horizontally                |
| `mirror_y`                 | 0/1               | 0        | Flip cat vertically                  |
| `enable_hand_mapping`      | 0/1               | 1        | Map keys to left/right hand frames   |
| `keypress_duration`        | ms                | 100      | How long key-down frame is held      |
| `idle_frame`               | 0-4               | 0        | Frame shown when idle                |
| `idle_sleep_timeout`       | seconds           | 0        | Sleep after idle (0=disabled)        |
| `agent_done_timeout`       | 0-3600 seconds    | 5        | Return done to idle (0=stay)         |
| `agent_stale_timeout`      | 0-86400 seconds   | 600      | Clear stale work (0=disabled)        |
| `hotplug_scan_interval`    | seconds           | 30       | Device rescan interval (0=once)      |
| `enable_scheduled_sleep`   | 0/1               | 0        | Enable time-based sleep schedule     |
| `sleep_begin`              | HH:MM             | 00:00    | Sleep schedule start time            |
| `sleep_end`                | HH:MM             | 00:00    | Sleep schedule end time              |
| `disable_fullscreen_hide`  | 0/1               | 0        | Keep overlay visible in fullscreen   |
| `enable_debug`             | 0/1               | 0        | Enable debug logging                 |
| `test_animation_duration`  | ms                | 200      | Test animation frame duration        |
| `test_animation_interval`  | seconds                | 0        | Test animation repeat interval       |

Monitor selection and appearance are reconciled during reload. Named outputs
that are disconnected wait for reconnection; automatic selection uses the first
available output. `--monitor NAME` remains a startup override.

Use `[monitor:NAME]` for appearance overrides and `[global]` to return to global
settings. Global defaults apply before overrides regardless of section order.
Supported overrides: `cat_height`, `overlay_height`, `overlay_opacity`,
`cat_x_offset`, `cat_y_offset`, `layer`, `overlay_position`, `cat_align`,
`mirror_x`, `mirror_y`, `enable_antialiasing`, and `disable_fullscreen_hide`.
Input selectors and animation timing remain global.

```ini
monitor=eDP-1,HDMI-A-1
cat_height=40
[monitor:HDMI-A-1]
cat_height=60
mirror_x=1
[global]
fps=60
```

Startup tolerates malformed entries with warnings. Reloads and `--check-config`
are strict: invalid, missing, or unreadable files leave the running config
active. Watching tracks the parent directory, including atomic replacements,
and reloads 300 ms after the final relevant event.

</details>

## Command Line

```bash
bongocat [OPTIONS]

  -c, --config FILE    Config file path (default: auto-detect)
  -m, --monitor NAME   Force specific monitor output
  -w, --watch-config   Auto-reload on config change
  -t, --toggle         Start/stop toggle
  --check-config      Validate config without Wayland or input access
  --list-devices      List devices, capabilities and access errors
  --list-monitors     List monitor names, dimensions and scales
  --doctor            Check config, protocols, outputs and input permissions
  --hide / --show     Change visibility of every overlay
  --pause / --resume  Show idle frame, discard input, or resume animation
  --state NAME        Set manual state: idle, working, waiting, done
  --sessions          List tracked agent sessions
  --hook AGENT        Read a lifecycle event from stdin
  --reload / --status Reload config or query the running instance
  -h, --help           Help
  -v, --version        Version
```

Controls use a user-owned Unix socket and require the same UID. Hide preserves
animation state; pause displays the configured idle frame and discards input.
Resume clears pending paw activity. Hidden and paused state reset on restart;
controls never rewrite config files. Failed commands return nonzero.

Agent sessions are resolved by priority and share one indicator across outputs.
`--status` reports `agent=NAME` and `sessions=N`; `--sessions` lists each session.
See the session model and hook setup below.

When both input paths and names are empty, accessible keyboard-capable evdev
devices are selected automatically. Explicit selectors never fall back to an
unrelated device. Stable `/dev/input/by-id/` and `/dev/input/by-path/` aliases
are supported and devices are deduplicated by identity. No permission changes
are made automatically. Keycodes are never transmitted or logged, including
with `enable_debug=1`.

## Agent status

This fork keeps the keyboard paw animation and tracks up to 32 agent sessions.
The shared indicator displays the highest priority state:
**waiting > done > working > idle**. A working session cannot overwrite another
session's waiting state. Each done session expires independently after
`agent_done_timeout` seconds (default 5); the display then falls back to any
remaining work. Setting the timeout to 0 keeps that session done until its next
event or removal.

```bash
bongocat --sessions        # Agent, session key prefix, state, age and process
bongocat --status          # Includes agent=NAME and sessions=N
bongocat --state working   # Set a separate manual session
bongocat --state waiting
bongocat --state done
bongocat --state idle      # Remove the manual session
```

Sessions disappear when their agent process exits, using pidfd/epoll watches.
`agent_stale_timeout` defaults to 600 seconds (range 0–86400; 0 disables it).
Working sessions without further events return to idle after that interval.
Waiting sessions use the same fallback only when no process watch is available;
a watched waiting session can wait indefinitely for a user. Identical events
refresh the timestamp without redrawing. When all 32 slots are occupied, the
oldest idle session is evicted first, otherwise the oldest session is evicted.
Restarting the overlay clears the session table.

The frame priority is scheduled sleep, held paws, agent artwork, idle sleep,
then `idle_frame`. Only original frames 0–4 can be configured as idle. Pause
shows the idle frame while session deadlines and process watches keep running;
resume shows the current resolved state. The indicator is shared across outputs.

### Agent status integration

1. Install this fork (`make release && sudo make install`). Confirm that
   `command -v bongocat` selects it and `bongocat --help` includes `--hook`;
   upstream v2.0.2 does not support this option.
2. Keep your config at `~/.config/bongocat/bongocat.conf` and run `bongocat -w`.
   For niri startup, add `spawn-at-startup "bongocat" "-w"`. Keep
   `enable_debug=0` for normal use.
3. Back up and merge [the Claude Code example](examples/hooks/claude-code.settings.json)
   into `~/.claude/settings.json`, and
   [the Codex example](examples/hooks/codex.hooks.json) into
   `~/.codex/hooks.json` (or `$CODEX_HOME/hooks.json`). Replace earlier bongocat
   `--state` hooks with the new definitions; preserve unrelated hooks/settings.
   Do not replace whole configuration files with these examples.
4. Start new agent sessions to load the hooks. In Codex, keep `[features] hooks`
   enabled, open `/hooks`, review and trust the new bongocat definitions.
   Modified definitions require renewed trust. Restart an older overlay after
   installing the new binary.

Every event for an agent uses the same command; the client reads stdin JSON and
performs event filtering itself, without jq:

```sh
bongocat --hook claude >/dev/null 2>&1 || true
# Use --hook codex for Codex.
```

| Event | Session action |
| --- | --- |
| `SessionStart` | Register as idle; preserve an existing state |
| `UserPromptSubmit`, `PreToolUse`, `PostToolUse` | working |
| `PostToolUseFailure` (Claude) | working |
| `PermissionRequest` | waiting |
| `Notification`: `permission_prompt`, `elicitation_dialog`, `agent_needs_input` | waiting (Claude) |
| `Notification`: `idle_prompt` | Clear working only; preserve waiting/done (Claude) |
| `Stop` | done, except when `stop_hook_active=true` |
| `StopFailure` (Claude), `Interrupt` (Codex) | idle |
| `SessionEnd` | Remove this session |
| Other events, including `SubagentStop` | Ignore |

Approval takes effect before the next hook: waiting remains until the tool
finishes and emits `PostToolUse`. Long running approved tools can therefore still
show waiting. In the tested Claude version, Esc did not produce Stop or an
idle notification during a 65-second observation. Working then relies on the
stale timeout; watched waiting persists until another event or process exit.
Codex emits Interrupt, which clears the interrupted session immediately.
Subagent tools share the parent session ID; a subagent finishing is not a whole
session completion. A parent can also finish a turn while a child runs in the
background. See [observed lifecycle details](docs/agent-hook-observations.md).

Missing or invalid session IDs use one fallback session per agent. The client
uses fixed storage, streams large payloads, rejects nesting beyond 128 levels,
and exits within two seconds if stdin stalls. Unknown events, malformed JSON,
and an absent overlay quietly return success. Stdout stays empty. Only invalid
CLI arguments return nonzero. For diagnosis, run the client without stderr
redirection and set `BONGOCAT_HOOK_DEBUG=1` to print the outgoing event request.

The Codex SessionEnd example has a one-second hook timeout. Hook execution must
share the desktop user's UID and runtime directory; remote/cloud agents cannot
control a local overlay through this socket. See the
[Claude Code hooks reference](https://code.claude.com/docs/en/hooks) and
[Codex hooks reference](https://learn.chatgpt.com/docs/hooks) for your installed
version's event and trust behavior.

## Troubleshooting

<details>
<summary>Permission denied on input device</summary>

```bash
sudo usermod -a -G input $USER
# Then log out and back in
```

</details>

<details>
<summary>Cat not responding to keyboard</summary>

1. Run `bongocat-find-devices` to find correct device
2. Update `keyboard_device` in config
3. Restart bongocat

</details>

<details>
<summary>Not showing on correct monitor</summary>

Set `monitor=YOUR_MONITOR` (single) or `monitor=MON1,MON2` (multi) in config. Find names with `wlr-randr` or `hyprctl monitors`.

</details>

## Building

```bash
git clone https://github.com/saatvik333/wayland-bongocat.git
cd wayland-bongocat
make          # Release build
make debug    # Debug build
```

**Requirements:** wayland-client, gcc/clang, make

## License

MIT License - see [LICENSE](LICENSE)
