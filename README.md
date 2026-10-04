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

### Arch package for this fork

The VCS package builds `feature/agent-state` and replaces packages providing
`bongocat`. From the repository root:

```bash
cd packaging/arch
makepkg -si
```

The package installs under `/usr`. If this fork was previously installed with
`sudo make install`, remove those `/usr/local` files after the package installs
successfully so they do not shadow `/usr/bin/bongocat`:

```bash
# From the repository root; preserves user configuration and saved positions.
sudo make PREFIX=/usr/local uninstall
```

Manual installs still default to `/usr/local`; override with `PREFIX` and use
`DESTDIR` for staging. No package script changes input-device permissions.

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

## Dragging

Hold the left mouse button on the cat and move it horizontally or vertically.
A movement of at least four logical pixels starts a drag; clicking alone keeps
the position. With cursor-shape support the cursor changes to grab/grabbing.
`--doctor` reports whether the protocol is available.

Each output saves its position on release to
`${XDG_STATE_HOME:-$HOME/.local/state}/bongocat/position`. Positions survive
restart, reload and output reconnection, and are clamped when output dimensions
change. `bongocat --reset-position` removes saved positions for every output
and restores `cat_align` / `cat_x_offset` and zero vertical margin. Configuration
files are never rewritten. `overlay_position` chooses the edge from which the
saved vertical margin is measured; `cat_y_offset` still applies inside the bar.

The cat's bounding rectangle intercepts clicks; the rest of the bar remains
click-through. Hidden cats intercept no clicks, including fullscreen hiding
when enabled. Pause still allows dragging. Set `cat_draggable=0` globally or
in a monitor section for complete click-through. Dragging between outputs is
not supported; each output has its own cat and saved position.

## Session signs

Each session gets a sign named after its working directory. Claude uses a
rounded rectangle and Codex a circle. Click a plate to jump to its terminal on
niri, including across workspaces; dragging a plate moves the whole cat.
Missing window targets shake briefly. The cat keeps its typing and sleep frames.

A kitty split is focused too when that session's process has both
`KITTY_WINDOW_ID` and `KITTY_LISTEN_ON`. Add these lines to kitty.conf
yourself; this program does not edit that file:

```
allow_remote_control socket-only
listen_on unix:${XDG_RUNTIME_DIR}/kitty
```

`socket-only` accepts commands from the socket and refuses them from the
terminal. `listen_on` is that socket. Kitty expands the path and adds its
process id. Any local program that can open the socket can control kitty,
including typing into its windows and reading them. Restart kitty after the
edit. Reloading the config does not apply `listen_on`. Without the socket, a
click still focuses the kitty window and leaves the split alone.

The typing desk follows one session and stops at the first answer. A reported
kitty split selects the session in that split. A click selects that sign until
niri focus leaves its window. Otherwise the newest session in the focused
window is used, including when no split has been reported. Moving to another
split retracts the current desk. The new split's desk appears when you type
there, not merely because focus moved. Unread completions narrow only when a
split report exists: only that split is marked seen, and a report that matches
no session marks none of them. With no report, focusing the window still marks
every session in it seen. Clicking a sign acknowledges that sign and does not
change which other split counts as seen.

A kitty watcher can report which split is focused. Copy
`integrations/kitty/bongocat_watcher.py` into the kitty config directory and
add one line:

```
watcher bongocat_watcher.py
```

Kitty allows more than one `watcher` line. Keep an existing watcher, such as
`focus_opacity.py`, and do not replace it. Reloading kitty.conf applies this
watcher only to windows and splits opened afterward. The script sends the
same `pane` request on bongocat's control socket. It does not start a
process and does not use `kitten @`, so it needs no extra remote-control
permission. It gives up within a few tens of milliseconds when the cat is
not running, and it writes nothing to the terminal.

**Fan** (default) raises plates behind the cat. Hover a plate for its name;
waiting plates rise higher, sway and show their names automatically.

![Fan signs, synthetic renderer capture](docs/screenshots/session-signs/fan.png)

**Post** stacks boards on a paper-white outlined pole. Hover the cat to expand
all selected names together. Idle signs appear on hover by default; signs close
150 ms after leaving. Positions follow creation order, with active and recently
updated sessions preferred when the display limit is exceeded.

![Post signs, synthetic renderer capture](docs/screenshots/session-signs/post.png)

These captures use the production renderer and synthetic sessions on a plain
background; they contain no desktop content. The four archived
[design sources](docs/design/session-signs/) define appearance. Their external
blob images and support.js are not included; post tilt is intentionally omitted.

Unread completions stay green with a dot until you visit their window, click
the sign, submit again or end the session. Visiting/clicking starts the normal
completion timer. A completion in the focused window is already seen. When a
kitty split report is available, only that split is seen. Without niri focus
tracking, completions remain unread until clicked or submitted again.

Typing in an agent's focused terminal moves its sign under the paws as a name
board. It leaves after 2.5 seconds without typing, focus loss, submission or
hiding. Its slot stays reserved and the board passes pointer clicks through.
This requires niri's event stream and uses activity only, never key contents.

| Global option | Values (default first) |
| --- | --- |
| `sign_style` | `fan`, `post`, `off` |
| `sign_max` | `5`; range 1–5 |
| `sign_idle` | `hover`, `always`, `never` |
| `sign_font` | empty = system sans-serif; Fontconfig family, up to 127 bytes |
| `sign_font_size` | `13`; range 10–20, metadata/desk text proportional |
| `sign_animations` | `full`, `reduced` (transitions only), `off` (instant) |
| `sign_language` | `auto`, `en`, `zh` |
| `sign_done` | `sticky`, `timeout` |
| `sign_typing_desk` | `1`, `0` |

All options reload through `-w` or `--reload`. `auto` uses nonempty `LC_MESSAGES`,
then `LANG`: zh locales select simplified Chinese, others English. Use `zh` to
keep the earlier fixed Chinese text. Sizes scale with cat height and output
scale. Larger text leaves less room for names. `never` hides idle signs even
on hover. `timeout` also acknowledges existing unread completions on reload;
returning to `sticky` applies to subsequent completions. `off` restores the
original surface height, cat-only input region and whole-cat agent artwork;
`sign_done` still applies. The temporary `BONGOCAT_SIGN_STYLE` override is gone.

FreeType and Fontconfig are required. Only niri supports terminal jumping and
focus tracking. tmux panes stay on the shared window. A kitty click reaches the
matching split only when the socket above is set. Without a split report, kitty
splits still share that window: the desk takes the newest session, and focusing
the window marks every session in it seen. Text supports Latin and CJK with
fallback, but lacks
ligatures, right-to-left shaping and combining marks. At most five of the 32
tracked sessions are displayed. Approval still stays yellow until the tool
finishes; Claude Escape has no hook and retains the existing timeout limitation.
No sign-related periodic wakes remain when there are no sessions, hover or
keys. Reduced/off disable loops, including the desk caret blink; visible working
duration labels still update once a minute.

## Agent status

This fork keeps the keyboard paw animation and tracks up to 32 agent sessions.
[Native screenshots and validation](docs/agent-validation.md) show the final
artwork and tested runtime behavior.

The shared indicator displays the highest priority state:
**waiting > done > working > idle**. A working session cannot overwrite another
session's waiting state. Each seen done session expires independently after
`agent_done_timeout` seconds (default 5); unread completions remain sticky by
default. After a timed completion, the display then falls back to any
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
Restarting the overlay reads `$XDG_RUNTIME_DIR/bongocat/sessions` back. The
file is written atomically, mode 0600, and updates are coalesced instead of
landing on every event. A saved process that is no longer running is dropped. A saved process
with no controlling terminal is dropped too.
Working and waiting return as idle, because events during the restart were
missed. An unread completion stays unread. The recording path is kept and the
watch attaches again the next time that session is working. A session with no
pid is kept: there is no process to check. Moving the pointer onto the cat
looks at process names once. A Claude, Codex, Grok, or Kimi process that has a controlling terminal,
whose ancestor owns a terminal window, and which has no session yet, gets an
idle sign. Inside a git checkout the name is the repository root's directory.
Otherwise it is the current directory's own name. That look does not repeat until the
signs close and open again. Cursor, Copilot, Pi, and opencode are left out.

With `sign_style=off`, frame priority is scheduled sleep, held paws, agent artwork, idle sleep,
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
| `StopFailure` (Claude) | idle |
| `Interrupt` (Codex) | Clear working/waiting only; preserve done |
| `SessionEnd` | Remove this session |
| Other events, including `SubagentStop` | Ignore |

Approval takes effect before the next hook: waiting remains until the tool
finishes and emits `PostToolUse`. Long running approved tools can therefore still
show waiting. In the tested Claude version, Esc did not produce Stop or an
idle notification during a 65-second observation. The optional transcript monitor handles that interruption; without a usable
recording path the stale timeout remains the fallback.
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

### Grok (experimental)

Grok 1.0.46 was observed twice. The October 3 capture saw startup, submission
and failure payloads, but no successful model turn. On October 4 a separate
Grok child, model grok-4.5 at low effort, was observed by Grok itself; this
interactive session was not the process under test. A no-tool turn went idle,
working, done, then left when the process exited. A tool turn under
always-approve followed that path and never showed waiting. An invocation-only
`--permission-mode default` prompt did show waiting; interrupting it moved the
sign to idle, and the process exit then removed the session. Interrupting a
running command removed the sign while that process was still alive, with no
sampled idle state, so that interrupt path remains unverified.
Merge [grok.json](examples/hooks/grok.json) into a new file in
`~/.grok/hooks/` (or `$GROK_HOME/hooks/`), preserving existing hooks. Grok may
also load Claude/Cursor compatibility hooks; check `/hooks` for duplicates.
Restart the agent after merging. No installer changes user configuration.

| Grok event (PascalCase or snake_case) | Action |
| --- | --- |
| SessionStart | Register idle; preserve existing state |
| UserPromptSubmit, PreToolUse, PostToolUse, PostToolUseFailure | working |
| PermissionRequest; permission_prompt/elicitation_dialog/agent_needs_input notification | waiting |
| Stop | done unless stop_hook_active/stopHookActive is true |
| StopFailure, StopCancelled, Interrupt | interrupt: clear working/waiting only |
| idle_prompt notification | Clear working only |
| SessionEnd | Remove session |
| Other events | Ignore |

Both snake_case and camelCase field aliases are recognized; snake_case keys
win if both are supplied. Stdout is exactly `{}` plus newline, including invalid
JSON, unknown events, timeout or an absent overlay. Do not redirect stdout.
Errors temporarily clear active work; the proposed separate error state is not
implemented. A late interrupt cannot erase an unread completed sign.

### Kimi Code

Kimi Code 2.1.1 normal turns, approval/rejection, interruption and interactive
exit were observed. Back up `~/.kimi-code/config.toml` privately, then append
[kimi-code.toml](examples/hooks/kimi-code.toml) without changing the existing
model/provider configuration. Entries use only `event`, `command` and `timeout`;
unknown hook keys can invalidate the whole hooks section. Run `kimi doctor
config` before restarting Kimi. The hook writes no stdout.

| Kimi event | Action |
| --- | --- |
| SessionStart | Register idle; preserve existing state |
| UserPromptSubmit, PreToolUse, PostToolUse, PostToolUseFailure | working |
| PermissionRequest | waiting |
| PermissionResult (approved or rejected) | working; later events settle the turn |
| Stop | done unless stop_hook_active is true |
| Interrupt, StopFailure | Clear working/waiting only |
| SessionEnd | Remove session |

Successful print-mode exit did not emit SessionEnd in the observation; the
process watch removes its session. Do not infer print-mode SessionEnd support
from the interactive exit capture.

### Cursor Agent

Cursor Agent 2026.10.01-e373342 normal and interrupted shell turns were observed.
Merge [cursor.hooks.json](examples/hooks/cursor.hooks.json) into
`~/.cursor/hooks.json`, preserving `version: 1` and existing hooks, then restart
Cursor Agent. Do not add `beforeShellExecution`. Cursor waits for that hook
before it runs the command, and the adjacent `preToolUse` already reports
working. The adapter still maps `beforeShellExecution` to working if a config
includes it. The hook returns `{}` plus newline, including failure paths.
Identity prefers `conversation_id`; directory prefers the first string in
`workspace_roots`, with `session_id`/`cwd` aliases for other payloads.

| Cursor event | Action |
| --- | --- |
| sessionStart | Register idle; preserve existing state |
| beforeSubmitPrompt, preToolUse, postToolUse | working |
| stop with status=completed | done |
| stop with status=aborted or error | Clear working/waiting only |
| sessionEnd | Remove session |
| stop with absent/unknown status; afterShellExecution; postToolUseFailure | Ignore |

Cursor has no independently observed approval-request callback, so it never
shows waiting. The observed interruption sends failure/tool callbacks after
stop(aborted); ignoring those callbacks prevents an interrupted sign from
returning to working. No account addresses or real payloads are used as fixtures.

### GitHub Copilot CLI

Copilot CLI 1.0.27 normal turns, permission callbacks and cancellation were
observed. Merge [copilot.hooks.json](examples/hooks/copilot.hooks.json) into
`~/.copilot/hooks/hooks.json`, retaining existing hooks, then restart Copilot.
Commands use `bash` and `timeoutSec`, with `--event` on every entry: most
payloads do not identify the event. Stdout is `{}` plus newline.

| Copilot event | Action |
| --- | --- |
| sessionStart | Register idle; preserve an already-working session |
| userPromptSubmitted, preToolUse, postToolUse, postToolUseFailure | working |
| notification with notification_type=permission_prompt | waiting |
| agentStop with stopReason=end_turn | done |
| errorOccurred; agentStop with error/aborted/interrupted | Clear working/waiting only (error display pending) |
| sessionEnd | Remove session |
| permissionRequest; unknown notifications or stop reasons | Ignore |

permissionRequest occurs even for auto-allowed tools, so it never means waiting.
Only the observed permission_prompt notification maps to waiting. Submission
can precede sessionStart; that later start does not reset working. Only end_turn
has been verified as a successful stop reason; missing/new reasons do not turn
the sign green. Running-command Escape produced no hook during the four-second
observation, so interruption detection remains a limitation. The errorOccurred
and error/aborted/interrupted stop-reason mappings are defensive and were not
exercised in the successful model probe. Other stop reasons are ignored.

### Pi

Pi 0.84.2 supports [integrations/pi/bongocat.ts](integrations/pi/bongocat.ts)
without a build or npm dependencies. Copy that file to
`~/.pi/agent/extensions/bongocat.ts`, or test it with `pi -e /path/to/bongocat.ts`.
Restart/reload Pi to load it. The extension writes no stdout, changes no tool or
permission decisions, and silently ignores client/spawn failures.

| Pi extension event | Action |
| --- | --- |
| session_start | Register idle |
| before_agent_start, agent_start, tool_call, tool_result | working |
| agent_end: last assistant stopReason=stop | done |
| agent_end: error, aborted or length | Clear working/waiting only |
| agent_end: missing/unknown reason | Ignore |
| session_shutdown | Remove session |

Identity/directory come from the extension context. Only status, session ID,
directory and Pi's own numeric PID are sent to the child; prompts, tool output
and error messages are never sent. The C client checks that this PID matches
its discovered parent. The bridge serializes asynchronous sends in a bounded
128-entry queue without awaiting them in agent callbacks; overflow is dropped.
Forked/child sessions with a parentSession header are ignored. There is no
default permission hook, hence no waiting sign. The normal stop and error on
RPC abort were observed; aborted/length are defensive mappings. Physical Escape
and third-party subagent extensions have not been revalidated here.

The Pi event-registration pattern is adapted from
[OpenPets](https://github.com/OpenPetsHQ/openpets/tree/main/packages/pi),
copyright 2026 OpenPets, MIT. The full notice is retained in the bridge file.
The context/PID handoff and conditional completion mapping follow our local
observations rather than OpenPets' blanket success-on-agent_end behavior.

### opencode 2.x

For opencode 2.0.22, copy the directory
[integrations/opencode](integrations/opencode) to a permanent local directory,
then append its **absolute directory path** to the effective configuration's
`plugin` array. Preserve every existing plugin entry. This host reads both
`opencode.json` and `opencode.jsonc`; its plugin declaration was in jsonc.
Check the effective configuration on your host rather than assuming one file
wins. Reload opencode after merging; there is no installer or npm dependency.
The plugin's default `{id, setup}` export and async iterable subscription are
for v2; it is not a v1 plugin.

| opencode 2.x event | Action |
| --- | --- |
| session.created | Register idle |
| session.inbox.enqueued, session.execution.started, session.tool.called/success | working |
| permission.asked | waiting |
| permission.replied | working |
| session.execution.succeeded | done |
| session.execution.interrupted/failed | Clear working/waiting only |
| session.deleted | Remove session |
| Step failures, streaming messages and other events | Ignore |

The plugin runs in the **background service**. These sessions have PID 0:
terminal jumping and process-liveness monitoring are unavailable. A left click
acknowledges the sign instead of shaking. An unread completion stays up until
that click, then `agent_done_timeout` puts it away. An idle session with no
pid is removed after `agent_stale_timeout` with no further events. A session
that has a pid still leaves only when its process exits. Stopping a terminal
alone cannot remove an opencode session. There is no PID inference from shell
tools.

The bridge retains directory metadata by session ID, because completion/deletion
lack it. A first event performs one `ctx.session.get({sessionID})` lookup to get
`location.directory` and `parentID`; a separately created child session was
verified to have parentID both in its creation event and in the lookup result. Child sessions and sessions outside the plugin's directory are ignored.
Lookups that fail are silently skipped; no background scan or polling is used.
Both the metadata cache and asynchronous dispatch queue are bounded at 128.
Only the ID, directory and event name are sent, never conversation/tool/error
content. A missing client or stream failure cannot fail the agent operation.

The normal/interrupted event sequences were observed in 8.0; execution.failed
is a defensive mapping and has not been triggered in a real model round here.
Standalone mode had an empty plugin list in the probe, so it is not claimed to
work. Service plugin loading, root/child metadata lookup, creation and deletion
were checked with a separate service and temporary HOME, without model usage.
Live desktop acceptance remains part of the later installation step.

[OpenPets' opencode bridge](https://github.com/OpenPetsHQ/openpets/blob/main/packages/opencode/src/opencode-plugin-runtime.ts)
was reviewed as an MIT reference (copyright 2026 OpenPets; notice retained).
It uses v1 `event({event})`, `session.status` and tool callbacks, which are not
the observed v2 API; this bridge implements the locally verified v2 interface.

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

**Requirements:** wayland-client, FreeType, Fontconfig, pkg-config, gcc/clang, make

## License

MIT License - see [LICENSE](LICENSE)

### Interrupted and failed turns

`agent_interrupt_detect=1` (default, global and reloadable) watches Claude Code
and Codex recording files only while their sessions are working or waiting.
SessionStart/UserPromptSubmit hooks supply the top-level `transcript_path`;
there is no directory scan. Paths are limited to 1024 decoded bytes, must be
absolute `.jsonl` files under the current user's home, and must be regular files
owned by that user. Symlinks (including ancestors) and `..` components are
rejected. Disabling the option immediately releases all recording watches;
enabling it starts at the current end of each file.

Claude's observed single user text block beginning with
`[Request interrupted by user` returns working/waiting to idle. Matches read
within one second of submission are ignored. Codex `event_msg` records with
`payload.type=turn_aborted`, or `task_complete` with an `error` object, also
return working/waiting to idle. Normal Codex `task_complete` records do nothing:
Stop hooks retain ownership of normal completion and unread signs. Duplicate
interruptions cannot clear done or recreate an ended session. Errors currently
share this idle behavior; a separate error appearance is not implemented.

Only newly appended complete lines of at most 4096 bytes are inspected in
memory. No transcript content or error message is logged, saved or sent anywhere,
including with debug enabled. Reads are event driven, at most 256 KiB per wake,
and add no polling timeout. No active monitored session means no recording watch.

These are private recording formats observed in Claude Code 2.1.288/2.1.289 and
Codex CLI 0.160.0. Missing/unreadable paths, truncation/rotation, overlong lines,
escaped schema/marker spellings and changed formats silently fall back to the
existing stale timeout. Rotation/truncation requires another path handoff to
retry. A user literally submitting Claude's interruption marker can still be
misclassified if the record is read after the one-second guard. Very early real
Claude interruptions inside that guard can be missed. No file-based detection
is enabled for other agents. Copilot's observed Ctrl+C remains without a reliable
interrupt event; opencode still lacks terminal focus and process-liveness mapping.
