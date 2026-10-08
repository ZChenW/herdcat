# Agent status

herdcat keeps the keyboard paw animation and tracks up to 32 agent sessions.

The shared indicator displays the highest priority state:
**waiting > error > done > working > idle**. A working session cannot overwrite
another session's waiting state. A turn that stops on an error (quota, API
failure) turns the sign coral with a cross; like a completion it stays until
you have looked at it. Each seen done session expires independently after
`agent_done_timeout` seconds (default 5); unread completions remain sticky by
default. After a timed completion, the display then falls back to any
remaining work. Setting the timeout to 0 keeps that session done until its next
event or removal.

```bash
herdcat --sessions        # Agent, session key prefix, state, age and process
herdcat --status          # Includes agent=NAME and sessions=N
herdcat --state working   # Set a separate manual session
herdcat --state waiting
herdcat --state done
herdcat --state idle      # Remove the manual session
```

Sessions disappear when their agent process exits, using pidfd/epoll watches.
`agent_stale_timeout` defaults to 600 seconds (range 0–86400; 0 disables it).
Working sessions without further events return to idle after that interval.
Waiting sessions use the same fallback only when no process watch is available;
a watched waiting session can wait indefinitely for a user. Identical events
refresh the timestamp without redrawing. When all 32 slots are occupied, the
oldest idle session is evicted first, otherwise the oldest session is evicted.
Restarting the overlay reads `$XDG_RUNTIME_DIR/herdcat/sessions` back. The
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

## Agent status integration

Use `herdcat setup` to detect installed agents, preview changes and confirm them.
Python 3 is optional for the overlay and required only for setup. In a source
checkout, run `python3 scripts/herdcat-setup` directly, or put `scripts/` in
`PATH` before running `./build/herdcat setup`.

```sh
herdcat setup claude codex --dry-run
herdcat setup claude codex --yes
herdcat setup --status
herdcat setup --remove claude codex --yes
```

Agent names are `claude`, `codex`, `grok`, `cursor`, `copilot`, `kimi`, `pi`, and
`opencode`. Terminal adapters `tmux` and `kitty` are also available; see
[terminal support](signs.md#terminal-support). Without names, setup selects agents whose executable is in `PATH`
or whose configuration directory exists. Explicit names also allow preparing
configuration before installing an agent. Confirmation defaults to no; a
non-interactive terminal must supply `--yes`. `--dry-run` and `--status` never
write files, and output never includes existing configuration contents.

JSON merges preserve unrelated settings and hooks. Commands beginning with
`herdcat --hook ` or the legacy `bongocat --hook ` identify managed entries;
setup replaces obsolete events/commands and removes only these entries.
Grok's dedicated `hooks/herdcat.json` also preserves unrelated entries if they
are present. Kimi receives a block between `# >>> herdcat >>>` and
`# <<< herdcat <<<`; text outside the block is retained, except for recognized
simple legacy herdcat hook tables. Ambiguous markers/tables need manual repair.
Pi's extension and opencode's bridge occupy private herdcat paths; setup refuses
to overwrite unrelated code there. opencode JSONC edits retain comments and
trailing commas. Setup uses `opencode.jsonc`, or existing `opencode.json` when
there is no JSONC file, retaining the existing `plugin` or `plugins` field.
The legacy `plugin` field is accepted by the observed opencode 2.0.23 converter;
newer documentation calls this field `plugins`. Both configuration files can
contribute plugins; review the other file manually for duplicate declarations.

`CODEX_HOME`, `GROK_HOME`, `CLAUDE_CONFIG_DIR`, `COPILOT_HOME`, `KIMI_CODE_HOME`,
`PI_CODING_AGENT_DIR`, and `XDG_CONFIG_HOME` are respected where applicable.
The defaults below remain the manual reference. Kimi Code uses `.kimi-code/`;
the separate legacy kimi-cli project uses `.kimi/` and is not this adapter.

Before every changed file, setup saves a private backup directory under
`$XDG_STATE_HOME/herdcat/backups/setup-<timestamp>/<agent>/` (default
`~/.local/state/herdcat/backups/`). File modes are preserved; Kimi remains 0600.
Writes use temporary files and atomic replacement. Symlink targets are updated
and displayed; links remain intact. Invalid JSON/JSONC is left untouched with a
nonzero exit and a reference to the manual steps below.

Private receipts under the same state directory let removal restore the exact
original bytes when no subsequent edits occurred. When a config has changed
since setup, removal instead strips the managed entries and retains new user
settings. Keep backups/receipts until removal if exact formatting restoration
matters. Removing a migrated legacy bridge removes it rather than reinstalling
the obsolete integration. Setup never enables Codex features automatically:
enable `[features] hooks`, then use `/hooks` to review and trust changed hooks.
Restart/reload open agents after setup or removal.

### Manual integration reference

1. Install herdcat (`make release && sudo make install`) and confirm that
   `herdcat --help` includes `--hook`.
2. Keep your config at `~/.config/herdcat/herdcat.conf` and run `herdcat -w`.
   For niri startup, add `spawn-at-startup "herdcat" "-w"`. Keep
   `enable_debug=0` for normal use.
3. Back up and merge [the Claude Code example](../integrations/hooks/claude-code.settings.json)
   into `~/.claude/settings.json`, and
   [the Codex example](../integrations/hooks/codex.hooks.json) into
   `~/.codex/hooks.json` (or `$CODEX_HOME/hooks.json`). Replace earlier herdcat
   `--state` hooks with the new definitions; preserve unrelated hooks/settings.
   Do not replace whole configuration files with these examples.
4. Start new agent sessions to load the hooks. In Codex, keep `[features] hooks`
   enabled, open `/hooks`, review and trust the new herdcat definitions.
   Modified definitions require renewed trust. Restart an older overlay after
   installing the new binary.

Every event for an agent uses the same command; the client reads stdin JSON and
performs event filtering itself, without jq:

```sh
herdcat --hook claude >/dev/null 2>&1 || true
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
| `StopFailure` (Claude) | error, from working/waiting only |
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
background.

Missing or invalid session IDs use one fallback session per agent. The client
uses fixed storage, streams large payloads, rejects nesting beyond 128 levels,
and exits within two seconds if stdin stalls. Unknown events, malformed JSON,
and an absent overlay quietly return success. Stdout stays empty. Only invalid
CLI arguments return nonzero. For diagnosis, run the client without stderr
redirection and set `HERDCAT_HOOK_DEBUG=1` to print the outgoing event request.

The Codex SessionEnd example has a one-second hook timeout. Hook execution must
share the desktop user's UID and runtime directory; remote/cloud agents cannot
control a local overlay through this socket. See the
[Claude Code hooks reference](https://code.claude.com/docs/en/hooks) and
[Codex hooks reference](https://learn.chatgpt.com/docs/hooks) for your installed
version's event and trust behavior.

## Grok

Grok 1.0.46 was observed on October 3, 4 and 7. The October 3 capture saw
startup, submission
and failure payloads, but no successful model turn. On October 4 a separate
Grok child, model grok-4.5 at low effort, was observed by Grok itself; this
interactive session was not the process under test. A no-tool turn went idle,
working, done, then left when the process exited. A tool turn under
always-approve followed that path and never showed waiting. An invocation-only
`--permission-mode default` prompt did show waiting; interrupting it moved the
sign to idle, and the process exit then removed the session. The reviewer's
October 7 tmux capture verified Ctrl+C during `sleep 77`: a hook arrived and
the sign became idle within 0.5 seconds, retaining the live process and session.
Ctrl+C 0.5 seconds after submission emitted no later hook and returned the
prompt to the input box; quiet detection now handles that path. Esc does not
cancel a Grok turn. See [the observations](agent-hook-observations.md#early-cancellation-and-terminal-output-2026-10-07).
Merge [grok.json](../integrations/hooks/grok.json) into a new file in
`~/.grok/hooks/` (or `$GROK_HOME/hooks/`), preserving existing hooks. Grok may
also load Claude/Cursor compatibility hooks; check `/hooks` for duplicates.
Restart the agent after merging. Package installation never changes agent
configuration; `herdcat setup` does so only after confirmation or `--yes`.

| Grok event (PascalCase or snake_case) | Action |
| --- | --- |
| SessionStart | Register idle; preserve existing state |
| UserPromptSubmit, PreToolUse, PostToolUse, PostToolUseFailure | working |
| PermissionRequest; permission_prompt/elicitation_dialog/agent_needs_input notification | waiting |
| Stop | done unless stop_hook_active/stopHookActive is true |
| StopFailure | error, from working/waiting only |
| StopCancelled, Interrupt | interrupt: clear working/waiting only |
| idle_prompt notification | Clear working only |
| SessionEnd | Remove session |
| Other events | Ignore |

Both snake_case and camelCase field aliases are recognized; snake_case keys
win if both are supplied. Stdout is exactly `{}` plus newline, including invalid
JSON, unknown events, timeout or an absent overlay. Do not redirect stdout.
StopFailure maps active work to error. A late interrupt cannot erase an unread
completed sign.

## Kimi Code

Kimi Code 2.1.1 normal turns, approval/rejection, interruption and interactive
exit were observed. Back up `~/.kimi-code/config.toml` privately, then append
[kimi-code.toml](../integrations/hooks/kimi-code.toml) without changing the existing
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
| StopFailure | error, from working/waiting only |
| Interrupt | Clear working/waiting only |
| SessionEnd | Remove session |

Successful print-mode exit did not emit SessionEnd in the observation; the
process watch removes its session. Do not infer print-mode SessionEnd support
from the interactive exit capture.

## Cursor Agent

Cursor Agent 2026.10.01-e373342 normal and interrupted shell turns were observed.
Merge [cursor.hooks.json](../integrations/hooks/cursor.hooks.json) into
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
| stop with status=error | error, from working/waiting only |
| stop with status=aborted | Clear working/waiting only |
| sessionEnd | Remove session |
| stop with absent/unknown status; afterShellExecution; postToolUseFailure | Ignore |

Cursor has no independently observed approval-request callback, so it never
shows waiting. The observed interruption sends failure/tool callbacks after
stop(aborted); ignoring those callbacks prevents an interrupted sign from
returning to working. No account addresses or real payloads are used as fixtures.

## GitHub Copilot CLI

Copilot CLI 1.0.27 normal turns, permission callbacks and cancellation were
observed. Merge [copilot.hooks.json](../integrations/hooks/copilot.hooks.json) into
`~/.copilot/hooks/hooks.json`, retaining existing hooks, then restart Copilot.
Commands use `bash` and `timeoutSec`, with `--event` on every entry: most
payloads do not identify the event. Stdout is `{}` plus newline.

| Copilot event | Action |
| --- | --- |
| sessionStart | Register idle; preserve an already-working session |
| userPromptSubmitted, preToolUse, postToolUse, postToolUseFailure | working |
| notification with notification_type=permission_prompt | waiting |
| agentStop with stopReason=end_turn | done |
| errorOccurred; agentStop with error | error, from working/waiting only |
| agentStop with aborted/interrupted | Clear working/waiting only |
| sessionEnd | Remove session |
| permissionRequest; unknown notifications or stop reasons | Ignore |

permissionRequest occurs even for auto-allowed tools, so it never means waiting.
Only the observed permission_prompt notification maps to waiting. Submission
can precede sessionStart; that later start does not reset working. Only end_turn
has been verified as a successful stop reason; missing/new reasons do not turn
the sign green. Running-command Escape produced no hook during the four-second
observation, so interruption detection remains a limitation. The
aborted/interrupted stop-reason mappings are defensive and were not exercised.
errorOccurred was observed on a rejected model request (HTTP 400). Other stop reasons are ignored.

## Pi

Pi 0.84.2 supports [integrations/pi/herdcat.ts](../integrations/pi/herdcat.ts)
without a build or npm dependencies. Copy that file to
`~/.pi/agent/extensions/herdcat.ts`, or test it with `pi -e /path/to/herdcat.ts`.
Restart/reload Pi to load it. The extension writes no stdout, changes no tool or
permission decisions, and silently ignores client/spawn failures.

| Pi extension event | Action |
| --- | --- |
| session_start | Register idle |
| before_agent_start, agent_start, tool_call, tool_result | working |
| agent_end: last assistant stopReason=stop | done |
| agent_end: error or length | error, from working/waiting only |
| agent_end: aborted | Clear working/waiting only |
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

## opencode 2.x

For opencode 2.0.22, copy the directory
[integrations/opencode](../integrations/opencode) to a permanent local directory,
then append its **absolute directory path** to the effective configuration's
`plugin` array. Preserve every existing plugin entry. This host reads both
`opencode.json` and `opencode.jsonc`; its plugin declaration was in jsonc.
Check the effective configuration on your host rather than assuming one file
wins. opencode 2.0.23 also accepts the newer `plugins` field. Reload opencode
after merging; the bridge needs no npm dependency.
The plugin's default `{id, setup}` export and async iterable subscription are
for v2; it is not a v1 plugin.

| opencode 2.x event | Action |
| --- | --- |
| session.created | Register idle |
| session.inbox.enqueued, session.execution.started, session.tool.called/success | working |
| permission.asked | waiting |
| permission.replied | working |
| session.execution.succeeded | done |
| session.execution.failed | error, from working/waiting only |
| session.execution.interrupted | Clear working/waiting only |
| session.deleted | Remove session |
| Step failures, streaming messages and other events | Ignore |

The plugin runs in the **background service**. These sessions have PID 0:
terminal jumping and process-liveness monitoring are unavailable. A left click
acknowledges the sign instead of shaking. An unread completion or error is
also acknowledged after `agent_stale_timeout` without further events (600
seconds by default), then `agent_done_timeout` puts it away. Setting
`agent_stale_timeout=0` disables this automatic acknowledgement. Sessions with
a PID still wait to be seen. An idle session with no
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

## Interrupted and failed turns

`agent_interrupt_detect=1` (default, global and reloadable) watches Claude Code
and Codex recording files while their sessions are working or waiting. A Claude
session made idle by quiet detection may retain that watch for recovery below.
SessionStart/UserPromptSubmit hooks supply the top-level `transcript_path`;
there is no directory scan. Paths are limited to 1024 decoded bytes, must be
absolute `.jsonl` files under the current user's home, and must be regular files
owned by that user. Symlinks (including ancestors) and `..` components are
rejected. Disabling the option immediately releases all recording watches;
enabling it starts at the current end of each file.

Claude's observed single user text block beginning with
`[Request interrupted by user` returns working/waiting to idle. Matches read
within one second of submission are ignored. Codex `event_msg` records with
`payload.type=turn_aborted` also return working/waiting to idle; a
`task_complete` with an `error` object turns the sign to error. Normal Codex `task_complete` records do nothing:
Stop hooks retain ownership of normal completion and unread signs. Duplicate
interruptions cannot clear done or recreate an ended session.

Claude Esc and Grok Ctrl+C shortly after submission can leave no later hook or
interruption record. Claude Code 2.1.292 kept `✳` in its title even while
working, so the renderer no longer infers interruption from that title.
With `agent_interrupt_detect=1`, Claude and Grok sessions whose own state is
working and whose registered process stdout points to `/dev/pts/<number>` are
also checked for quiet output. Only the `wchar` counter in `/proc/<pid>/io` is
interpreted; terminal contents are never read. Two adjacent one-second windows
with fewer than 256 bytes each return working to idle, preserving the process
and session. The first second after an event is protected. Waiting, unread
done/error and the display state derived from active children are unaffected.

Eligible working sessions are sampled once per second throughout the turn,
regardless of time since the last hook or recognized user/assistant record.
No eligible working session means no quiet detection timer. Non-terminal stdout,
unreadable process I/O and other agents skip detection; a read failure retries
only after another hook. Disabling the option removes the timer and recovery
watches immediately.

Cancellation normally takes 2–3 seconds to put the sign away, including during
long turns. Bursts or delayed event-loop dispatch can delay it further. A truly
working process with two quiet windows can be misclassified. Its next working
hook corrects the guess; a newly appended recognized Claude user/assistant
record can also correct it. Only sessions
made idle by quiet detection retain their existing event-driven recording
watch for that recovery; ordinary idle sessions do not. No record or hook
means the guess cannot be corrected automatically. Headless runs and unmeasured
agents retain their existing hooks, transcript detection and stale timeout.

Agents report that they are waiting for an answer but not that it was given;
the next event only comes when the approved tool finishes or the model speaks
again. On niri, a key press in the terminal of a waiting session shows it as
working straight away, provided the window has had focus for a moment and the
keys can be meant for no other session. If no hook event follows within 60
seconds the sign goes back to waiting.

Only newly appended complete lines of at most 4096 bytes are inspected in
memory. No transcript content or error message is logged, saved or sent anywhere,
including with debug enabled. Reads are event driven, at most 256 KiB per wake,
and add no transcript polling timeout. Watches remain only for working/waiting
sessions or an idle Claude session recovering from a quiet guess.

These are private recording formats observed in Claude Code 2.1.288/2.1.289 and
Codex CLI 0.160.0. Missing/unreadable paths, truncation/rotation, overlong lines,
escaped schema/marker spellings and changed formats silently fall back to the
existing stale timeout. Rotation/truncation requires another path handoff to
retry. A user literally submitting Claude's interruption marker can still be
misclassified if the record is read after the one-second guard. Very early real
Claude interruptions inside that guard can be missed. No file-based detection
is enabled for other agents. Copilot's observed Ctrl+C remains without a reliable
interrupt event; opencode still lacks terminal focus and process-liveness mapping.

### Reviewer cancellation observations (2026-10-07)

The reviewer measured these agents in tmux panes; sign states came from the
running herdcat's `--sessions`. These are supplied observations, separate from
this implementation's synthetic tests.

| Agent (version where recorded) | Idle terminal output | Working output | Mid-reply interruption | Cancel 0.5 s after submission | Finding |
| --- | --- | --- | --- | --- | --- |
| Kimi Code | About 0–16 bytes/s | 2–17 KB/s | Esc → immediately idle (Interrupt hook) | Esc → immediately idle | Quiet detection unnecessary |
| Pi | 0 | 20–50 KB/s | Esc → immediately idle | Esc → immediately idle | Unnecessary |
| Codex | 0–0.5 KB/s | At least 3 KB/s | Esc → immediately idle | Esc → immediately idle | Unnecessary |
| Cursor Agent 2026.10.01 | Usually 0–16 bytes/0.5 s, with a 1–10 KB burst every 1–2 s | 5–10 KB/s | Ctrl+C → immediately idle; Esc does not cancel | Ctrl+C → idle | Unnecessary; idle bursts also make the current criterion unsuitable |
| GitHub Copilot CLI 1.0.27 | Not measured | Not measured | Not measured | Not measured | Request blocked: configured model rejected by the server (`400 The requested model is not supported`) |
| opencode | About 30–60 bytes/s | About 40 KB/s | One Esc does not interrupt; UI asks for a second press | Not successfully measured | Undetermined |

Quiet detection remains enabled only for Claude and Grok, the two agents
confirmed to leave no signal after early cancellation. Other measured agents
already signal interruption; unsuccessful probes do not establish support.
Copilot needs another probe once it can send a request. No real agent sessions
were started for this implementation.

## Agents launched by another session

An agent process descending from the agent PID of a tracked session belongs
to that session. For example, Claude running `codex exec` produces a child
Codex row rather than a second sign. The renderer follows at most 32 parent
links, only at first registration and metadata events for an unmerged session.
There is no additional full `/proc` scan on tool events. The hook preserves the
actual PID of a headless agent as a candidate alongside its existing terminal
PID fallback. Confirmed children use their actual PID and the shared pidfd loop.

Nested agents all belong to the top tracked session. An absent parent or one
without a PID leaves the child independent; a later metadata event can merge
it after the parent registers. A child remains in the fixed 32-row session
table and `herdcat --sessions` appends `parent=<eight-character parent key>`.
It has no sign, priority, unread completion, waiting/error alert, title or
typing desk of its own. Only working/waiting children contribute to the parent's
agent label. The actual parent state remains independent: session/status
output, alerts, typing, focus and unread handling do not change. For sign display,
an idle or read done parent with active children looks and ranks as working,
with `Waiting on subagent N min` / `等待子代理 N 分钟` measured from the earliest
creation of a still-active child. Other parent states keep their own appearance.
Fan signs always show an active-child count badge (`9+` above nine), moved to
the left corner when the unread dot occupies the right. Post boards use `+N`.
See [sign display rules](signs.md#child-agent-labels) for layout and transitions.

END, process exit or a configured timeout removes a child. Child done/error
uses `agent_done_timeout` without waiting for acknowledgement; otherwise
the existing working/stale and unwatched waiting deadlines apply. Watched
waiting and PID-bound idle rows remain tied to process exit. Removing or
evicting the top session leaves child rows hidden with their recorded parent
key until their own END, exit or timeout. Relationships are kept in memory and use creation order so a
provisional parent adopting its real session key preserves its children.

## Names, transcript recovery and detached children

The first cwd delivered by a current hook becomes the session's start directory.
The first name uses that directory as before: the git root's final component,
or the directory's own name. Later cwd metadata changes the name only inside a
strict subdirectory of the start directory. Returning to the start directory,
its parent or another tree preserves the last name. The private session file
stores the start directory across restarts. Older name-only hooks still work
with their earlier update behavior. cwd is hex encoded on the authenticated
control socket, so spaces and newlines cannot split the metadata request.

An untitled Claude or Codex session with a known JSONL path can recover a
`title~=` from its first eligible user message, even if herdcat missed the
submission. Claude accepts non-meta user string content or a text block,
excluding tool results. Codex accepts its existing event-message or response-item
user records. A helper process reads at most the first 256 KiB, with complete
lines bounded to 4096 bytes and a one-second deadline. It uses the existing
home-confined, same-UID, no-symlink file opener. A session attempts recovery once
per lifetime; a missing, unsupported or empty record does not cause event-driven
retries. The normalized first line is at most 96 UTF-8 bytes; commands starting
with `/` are skipped. A later explicit title replaces the temporary one.
Titles and prompts stay in memory and the private mode-0600 session file;
neither is logged, including in debug mode.

When a child's ancestry is broken by backgrounding or reparenting, the hook
also sends the inherited `CLAUDE_PID` (its own environment first, then a bounded
read of the actual agent's environment). Normal ancestry takes precedence.
The daemon accepts the fallback only if it names another currently tracked
Claude session's process. A self PID, an unknown PID or a tracked non-Claude
PID is ignored. Claude is the only adapter declaring an owner environment
variable; no equivalents are guessed for other agents. Children keep their
own process watches while sharing the parent's sign and alert behavior.
