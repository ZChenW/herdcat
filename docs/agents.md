# Agents

herdcat learns about agent sessions through each agent's hooks or plugin. This
page covers connecting each supported agent, what its sign shows, and the known
limits of each integration.

## Connecting agents

`herdcat setup` finds installed agents, previews the changes and asks before
writing anything. It needs Python 3; the overlay itself does not. In a source
checkout without installing, run `python3 scripts/herdcat-setup` instead.

```sh
herdcat setup                              # detect agents, preview, confirm
herdcat setup claude codex --dry-run       # preview only
herdcat setup claude codex --yes           # write without asking
herdcat setup --status                     # check every integration
herdcat setup --remove claude codex --yes  # remove
```

Agent names are `claude`, `codex`, `grok`, `cursor`, `copilot`, `kimi`, `pi`,
`opencode`, `qwen` and `agy`. `tmux` and `kitty` set up pane focus for those
terminals (see [terminals](signs.md#terminals)).

Without names, setup selects agents whose executable is in `PATH` or whose
configuration directory exists. Naming an agent lets you prepare its
configuration before installing it. The confirmation defaults to no; when setup
does not run in a terminal it needs `--yes`. `--dry-run` and `--status` never
write files, and setup output never shows the contents of your existing
configuration. `--status` reports each integration as not installed, not
connected, connected, or connected but outdated.

Restart or reload open agents after setup or removal so they load the change.

### What setup changes

- **JSON settings** (Claude, Codex, Copilot, Cursor, Grok, Qwen, Antigravity):
  setup adds herdcat's hooks and keeps every unrelated setting and hook. Hooks
  that run `herdcat --hook` (or the older `bongocat --hook`) are herdcat's own;
  setup updates or removes only those.
- **Kimi Code** gets a block between `# >>> herdcat >>>` and
  `# <<< herdcat <<<` in its TOML config. Text outside the block is kept.
- **Pi** and **opencode** get a herdcat extension or plugin in a herdcat-owned
  location; setup refuses to overwrite unrelated files there. For opencode,
  setup edits `opencode.jsonc`, or `opencode.json` when there is no JSONC file,
  keeping comments and trailing commas.
- **Codex**: setup never turns on Codex features for you. Enable
  `[features] hooks` in Codex's config, then open `/hooks` in Codex to review
  and trust the new hooks. Changed hooks need to be trusted again.

Setup honours `CODEX_HOME`, `GROK_HOME`, `CLAUDE_CONFIG_DIR`, `COPILOT_HOME`,
`KIMI_CODE_HOME`, `PI_CODING_AGENT_DIR` and `XDG_CONFIG_HOME`. If a file is not
valid JSON or JSONC, setup leaves it untouched, exits with an error and points
to the manual steps below.

### Backups and removal

Before changing a file, setup copies it into a private backup directory,
`$XDG_STATE_HOME/herdcat/backups/setup-<timestamp>/<agent>/` (by default under
`~/.local/state/herdcat/backups/`). File modes are kept, writes are atomic,
and symlinks stay in place while their targets are updated.

`herdcat setup --remove` restores the original file exactly if nothing else
changed it since setup. Otherwise it removes only herdcat's entries and keeps
your newer settings. Keep `~/.local/state/herdcat/` until you have removed the
integrations if exact restoration matters to you.

### Connecting by hand

The templates setup uses are in [`integrations/`](../integrations) (installed
under `PREFIX/share/herdcat/integrations/`). Merge them into your existing
configuration; do not replace whole files with them. Every hook runs the same
command with the agent's name, for example:

```sh
herdcat --hook claude >/dev/null 2>&1 || true
```

The hook reads the event from standard input, quietly succeeds when herdcat is
not running, and gives up after two seconds if its input stalls. Set
`HERDCAT_HOOK_DEBUG=1` and run it
without the redirection to see what it would send.

Hooks must run as your desktop user on the same machine: remote or cloud agents
cannot reach the overlay.

## What a sign shows

| State | Meaning |
| --- | --- |
| Working | The agent is busy with a turn |
| Waiting | The agent is waiting for your approval or answer |
| Done | The turn finished; stays up with a dot until you have seen it |
| Error | The turn stopped on a failure such as a quota or API error; stays until seen |
| Idle | The session is open but not doing anything |

Seen completions go away after `agent_done_timeout` seconds (default 5; 0
keeps them until the session's next event). With `sign_style=off` the cat shows
the most urgent state of all sessions: waiting, then error, done, working and
idle.

A session disappears when its agent process exits. A working session that sends
no events for `agent_stale_timeout` seconds (default 600, 0 to turn off) returns
to idle. A waiting session is kept as long as its process runs, since you may
take a while to answer. herdcat tracks up to 32 sessions; when all are in use,
the oldest idle session makes room first.

Restarting herdcat restores its sessions from `$XDG_RUNTIME_DIR/herdcat/`.
Sessions whose process has ended are dropped, working and waiting sessions come
back as idle (events during the restart were missed), and unread completions
stay unread.

When you hover the cat, herdcat also looks once for running Claude, Codex,
Grok and Kimi processes in a terminal that have no session yet, and gives each
an idle sign.

The sign's name is the project: the git repository's directory name, or the
current directory's name outside a repository. When the agent moves into a
subdirectory of where it started, the name follows; moving elsewhere keeps the
last name. See [session names and titles](configuration.md#session-names-and-titles)
for titles.

```bash
herdcat --sessions        # agent, session key, state, age, process, name and title
herdcat --status          # includes agent=NAME and sessions=N
herdcat --state working   # set a separate manual session: working, waiting, done, error
herdcat --state idle      # remove the manual session
```

## Supported agents

| Agent | Setup name | Config file | Waiting shown | Error shown | Cancelling a turn |
| --- | --- | --- | --- | --- | --- |
| [Claude Code](#claude-code) | `claude` | `~/.claude/settings.json` | Yes | Yes | Returns to idle |
| [Codex](#codex) | `codex` | `~/.codex/hooks.json` | Yes | Yes | Returns to idle |
| [Grok](#grok) | `grok` | `~/.grok/hooks/herdcat.json` | Yes | Yes | Returns to idle |
| [Kimi Code](#kimi-code) | `kimi` | `~/.kimi-code/config.toml` | Yes | No | Returns to idle |
| [Cursor Agent](#cursor-agent) | `cursor` | `~/.cursor/hooks.json` | In tmux or kitty | Yes | Returns to idle |
| [GitHub Copilot CLI](#github-copilot-cli) | `copilot` | `~/.copilot/hooks/hooks.json` | Yes | Yes | Returns to idle in a terminal |
| [Pi](#pi) | `pi` | `~/.pi/agent/extensions/herdcat.ts` | No | Yes | Returns to idle |
| [opencode](#opencode) | `opencode` | `~/.config/opencode/opencode.jsonc` (or `.json`) | Yes | Yes | Returns to idle |
| [Qwen Code](#qwen-code) | `qwen` | `~/.qwen/settings.json` | Yes | Yes | Returns to idle |
| [Antigravity CLI](#antigravity-cli) | `agy` | `~/.gemini/config/hooks.json` | From its log | Yes | Returns to idle |

For every agent, a sign that is waiting for approval stays waiting until the
approved tool finishes, because agents report the request but not your answer.
Typing in that session's terminal shows it as working sooner (see
[signs](signs.md#what-a-sign-shows)).

### Claude Code

Template: [claude-code.settings.json](../integrations/hooks/claude-code.settings.json),
merged into `~/.claude/settings.json` (or `$CLAUDE_CONFIG_DIR`).

Claude shows every state. Pressing Esc to cancel may send no hook at all;
herdcat then notices the cancellation from Claude's session recording or from
the terminal going quiet (see [interrupted and failed turns](#interrupted-and-failed-turns)).
Claude's own session title is used when it exists.

### Codex

Template: [codex.hooks.json](../integrations/hooks/codex.hooks.json), merged
into `~/.codex/hooks.json` (or `$CODEX_HOME/hooks.json`).

Codex needs `[features] hooks` enabled and the new hooks trusted under
`/hooks`; changed hooks need to be trusted again. Cancelling a turn clears the
sign at once. A failed turn is shown as an error when
`agent_interrupt_detect=1` (the default), because herdcat reads that from
Codex's session recording.

### Grok

Template: [grok.json](../integrations/hooks/grok.json), saved as a new file in
`~/.grok/hooks/` (or `$GROK_HOME/hooks/`).

Grok may also load Claude- or Cursor-compatible hooks; check `/hooks` in Grok
for duplicates. Grok only asks for approval, and so only shows waiting, when it
runs with a permission mode that asks (for example `--permission-mode default`).
Cancel a turn with Ctrl+C; Esc does not cancel in Grok. If Ctrl+C sends no
hook, herdcat notices the terminal going quiet.

### Kimi Code

Template: [kimi-code.toml](../integrations/hooks/kimi-code.toml), appended to
`~/.kimi-code/config.toml` (or `$KIMI_CODE_HOME`). The older kimi-cli project,
which uses `~/.kimi/`, is not supported.

Back up the file first and leave your model and provider settings as they are.
Hook entries may only use `event`, `command` and `timeout`: an unknown key can
make Kimi reject the whole hooks section. Run `kimi doctor config` before
restarting Kimi. The installed hooks do not include a failed-turn event, so a
failed Kimi turn is not shown as an error.

### Cursor Agent

Template: [cursor.hooks.json](../integrations/hooks/cursor.hooks.json), merged
into `~/.cursor/hooks.json` keeping `version: 1` and your existing hooks.
Restart Cursor Agent afterwards. Do not add a `beforeShellExecution` hook:
Cursor waits for that hook before running each command, and it adds nothing.

Cursor sends no hook when it asks for approval. With
`agent_cursor_wait_detect=1` (the default), herdcat reads the visible screen of
the Cursor session's **tmux pane or kitty window** to spot the approval menu.
It only does so while a tool call has been pending for 1.5 seconds without
another hook, at most once a second, and goes back to working when the menu
disappears or the next hook arrives. The screen text is checked in memory and
discarded; see [security](security.md#cursor-approval-screen-check).

- In tmux this works without extra setup; inside kitty, tmux takes precedence.
- In kitty it needs the `get-text` remote-control permission in addition to
  what `herdcat setup kitty` grants. Change that line to
  `remote_control_password "" focus-window get-text` and reload kitty's
  configuration (restart if you are also adding `listen_on`). This lets any
  local program that can reach kitty's socket read kitty windows, so herdcat
  never adds it for you.
- In other terminals, and for Cursor runs without a terminal, the sign stays
  working while Cursor waits for approval.
- Only the English approval prompts for shell commands, MCP tools, file
  reads, writes and deletes, edits, and web search and fetch are recognized.
  Narrow terminals that wrap the menu, prompts changed by a Cursor update, or a
  menu scrolled out of the last 20 lines can be missed.

Set `agent_cursor_wait_detect=0` to turn the screen check off.

### GitHub Copilot CLI

Template: [copilot.hooks.json](../integrations/hooks/copilot.hooks.json),
merged into `~/.copilot/hooks/hooks.json`, keeping existing hooks. Restart
Copilot afterwards.

Cancelling a turn (press Esc twice) sends no hook. herdcat notices the
terminal going quiet and returns the sign to idle; Copilot runs without a
terminal stay working until `agent_stale_timeout`.

### Pi

Copy [integrations/pi/herdcat.ts](../integrations/pi/herdcat.ts) to
`~/.pi/agent/extensions/herdcat.ts`, or try it with `pi -e /path/to/herdcat.ts`,
then restart or reload Pi. No build or npm packages are needed.

The extension only reports state: it changes no tool or permission decisions
and writes nothing to the terminal. It sends the session ID, directory and Pi's
process ID, never prompts, tool output or error messages. Stock Pi has no
permission prompt, so there is no waiting sign. Forked and child Pi sessions
are ignored.

The extension's event handling is adapted from
[OpenPets](https://github.com/OpenPetsHQ/openpets) (MIT); the notice is kept in
the file.

### opencode

opencode 2.x only. Copy the directory [integrations/opencode](../integrations/opencode)
to a permanent location, then add its **absolute path** to the `plugin` (or
newer `plugins`) array of your opencode configuration, keeping existing
entries. opencode reads both `opencode.json` and `opencode.jsonc`, so check
both for duplicate plugin entries. Reload opencode afterwards; no npm packages
are needed.

The plugin runs in opencode's background service, so herdcat cannot tell which
terminal a session belongs to:

- Clicking an opencode sign acknowledges it instead of jumping to a terminal.
- Stopping a terminal does not remove its opencode session. An idle session
  goes away after `agent_stale_timeout` without events.
- An unread completion or error is marked seen after `agent_stale_timeout`
  without events; `agent_stale_timeout=0` turns that off.

Press Esc twice to cancel a turn; the sign returns to idle. Only the session
ID, directory and event name are sent, never conversation, tool or error
content. Child sessions and sessions outside the plugin's directory are
ignored. opencode's standalone mode is not known to load the plugin.

The plugin was written with
[OpenPets' opencode bridge](https://github.com/OpenPetsHQ/openpets) (MIT) as a
reference.

### Qwen Code

Template: [qwen-code.settings.json](../integrations/hooks/qwen-code.settings.json),
merged into `~/.qwen/settings.json`. Project settings in `.qwen/settings.json`
must be trusted in Qwen. Restart Qwen afterwards.

Do not add `MessageDisplay` hooks: they fire continuously while Qwen streams.
Esc returns the sign to idle.

### Antigravity CLI

Template: [antigravity.hooks.json](../integrations/hooks/antigravity.hooks.json),
merged into `~/.gemini/config/hooks.json` (project hooks:
`.agents/hooks.json`). When merging by hand, replace `/absolute/path/to/herdcat`
with the full path of your herdcat binary. Keep your existing named groups and
any unrelated entries in the `herdcat` group. `herdcat setup agy` fills in the
path for you.

Antigravity has no session start or end events: the sign appears with the first
request and disappears when the process exits. It has no approval hook either,
so herdcat reads Antigravity's own per-process log
(`~/.gemini/antigravity-cli/log/cli-*.log`) to notice approval prompts,
answers and cancellations. The log is read only in memory and nothing from it
is saved or sent. Choosing "No, cancel" at an approval prompt ends the turn and
the sign returns to idle.

This log format is private to Antigravity and may change. If herdcat cannot
read it, approval prompts show as working and a cancelled turn stays working
until `agent_stale_timeout`. When several sessions share one log, approval
prompts in it are ignored.

The hooks never write output, so they cannot approve or reject anything.

## Interrupted and failed turns

Some agents send no hook when you cancel a turn. With `agent_interrupt_detect=1`
(the default) herdcat fills the gap:

- **Claude Code and Codex**: herdcat watches the session recording file named
  in the agent's hooks while the session is working or waiting. A recorded
  cancellation returns the sign to idle; a Codex turn that ended in an error
  shows an error.
- **Antigravity CLI**: herdcat watches its log for cancellations (see above).
- **Claude Code, Grok and Copilot** running in a terminal: herdcat counts how
  many bytes the agent writes to its terminal, without reading them. Two quiet
  seconds in a row while the session claims to be working return the sign to
  idle, usually within three seconds of cancelling. The agent's next hook
  corrects a wrong guess.

herdcat only reads newly added lines, in memory, and never logs, saves or
sends their content. Recording formats are private to each agent; if one
changes, the sign falls back to `agent_stale_timeout`. Typing Claude's own
interruption marker as a prompt can be mistaken for a cancellation. Setting
`agent_interrupt_detect=0` stops all of this immediately.

## Agents started by another agent

An agent started by another tracked agent, for example Claude running
`codex exec`, joins its parent's sign: `Claude + Codex`. `herdcat --sessions`
lists it with `parent=` and its own state. A child's completion or error does
not raise an alert of its own. If the parent ends while a child is still
working, the child gets its own sign. Claude children that were moved to the
background are still recognized through the `CLAUDE_PID` variable Claude sets.
See [signs](signs.md#agents-started-by-another-agent) for how the label looks.
