# Configuration reference

## Options Reference

<details>
<summary>Click to expand all options</summary>

| Option                     | Values            | Default  | Description                          |
| -------------------------- | ----------------- | -------- | ------------------------------------ |
| `cat_height`               | 10-200            | 40       | Cat size in pixels                   |
| `cat_align`                | left/center/right | center   | Horizontal alignment                 |
| `cat_x_offset`             | any int           | 100      | Horizontal offset from alignment     |
| `cat_draggable`            | 0/1               | 1        | Drag with the left mouse button      |
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
| `agent_interrupt_detect`   | 0/1              | 1        | Watch interruption/error records    |
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
`mirror_x`, `mirror_y`, `cat_draggable`, `enable_antialiasing`, and
`disable_fullscreen_hide`.
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
  --pane PID ID       Report the focused kitty split
  --hook AGENT        Read a lifecycle event from stdin
  --reset-position    Restore configured positions on every output
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
