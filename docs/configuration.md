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
| `agent_interrupt_detect`   | 0/1              | 1        | Detect interrupted and failed turns |
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
herdcat [OPTIONS]

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
  --state NAME        Set manual state: idle, working, waiting, done, error
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
Its `input=` field is `connected` once a keyboard is open, `denied` when none is
and some devices refused access (`denied=N` counts them, and a second line says
how to grant access), `searching` while no keyboard exists, and `restarting`
while the input helper restarts.
See the session model and hook setup below.

When both input paths and names are empty, accessible keyboard-capable evdev
devices are selected automatically. Explicit selectors never fall back to an
unrelated device. Stable `/dev/input/by-id/` and `/dev/input/by-path/` aliases
are supported and devices are deduplicated by identity. No permission changes
are made automatically. Keycodes are never transmitted or logged, including
with `enable_debug=1`.

## Session names and titles

All four name settings are global and support config watching or `--reload`:

| Option | Default | Values |
| --- | --- | --- |
| `sign_name` | `project` | `project`, `title` |
| `sign_name_extra` | `inline` | `off`, `inline`, `end`, `above`, `below` |
| `sign_title_length` | `16` | 0–64 Unicode codepoints; 0 disables truncation |
| `sign_nameplate` | unset | Advanced fan template, at most 160 UTF-8 bytes |

`project` always shows the repository/directory name. `title` uses the session
title when available and falls back to the directory. The default is a bold
directory followed by a secondary title; absent or equal titles are omitted.
Sessions in the same directory keep the same main name, without numbering.

On fan nameplates, `inline` puts the other name before agent/status, `end` after
status; `above` and `below` add a secondary row. Equal names are not repeated.
Post boards show the extra after the bold main name for all four enabled
positions, with the main name nearest the icon on either side of the pole.
The extra uses the remaining space before agent/status, truncates with `…`,
and is omitted when less than 24 logical pixels remain. `off` omits the extra
on both styles. The typing desk shows only the main name.
Titles are codepoint-truncated with `…`; the post also applies its width limit.
If there is insufficient vertical clearance the fan's extra row is omitted.

For directory followed by title on its own row:

```ini
sign_name=project
sign_name_extra=below
sign_title_length=24
```

`sign_nameplate` overrides the extra setting on fan nameplates only; post
boards still follow `sign_name` and `sign_name_extra`. Allowed placeholders are `{name}`,
`{project}`, `{title}`, `{agent}` and `{state}`. `**...**` uses bold primary text;
other text uses the secondary font and colour. Waiting/error state text keeps
its accent. Literal `\n` separates at most two lines. Segments separated by
` · ` disappear if any placeholder in that segment is empty; empty lines also
disappear. `{title}` is empty when equal to `{name}`. Width overflow truncates
trailing secondary content before bold content. Invalid templates report a
byte position in strict checks/reload; tolerant startup warns and uses the
built-in extra template.

```ini
sign_name=project
sign_nameplate={agent} · **{name}**\n{state}
```

Session titles are read locally for Claude, Codex, Grok, Kimi Code, Pi and Copilot;
opencode forwards its session metadata title through the bridge. If no title
exists, the first prompt's normalized first line supplies a temporary title for
Claude, Codex, Grok, Kimi, Cursor and Copilot. Blank prompts and slash commands
are ignored. A real title replaces the temporary title and survives missing or
malformed source updates. `--sessions` marks temporary titles with `title~=`.
They remain in memory and the mode-0600 session file under
`$XDG_RUNTIME_DIR/herdcat/`; only `--sessions` deliberately displays them. Titles
and prompt excerpts never enter logs. Only the local authenticated control
channel carries their bounded metadata; hook debug output redacts it. No new
thread, periodic poll, or external title upload is used.
