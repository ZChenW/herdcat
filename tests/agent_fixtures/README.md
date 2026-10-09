# Recorded agent hook fixtures

Only attributable payloads recorded from a real agent belong here. Do not
reconstruct missing payloads from event tables, adapter rules, or synthetic
runtime tests. `manifest.json` inventories all ten adapters; an empty scenario
list explicitly means **no raw payload available**, even when a document gives
an observation version or an event sequence.

There are recordings from Qwen Code, Antigravity CLI, Claude Code, Codex,
Copilot CLI and Pi. See the manifest and per-scenario provenance for exact
versions and partial validation boundaries. Every scenario has `provenance.json` with the recorded version,
the source capture's SHA-256 and line numbers, what was anonymized and the CLI
arguments. The raw captures are not in the repository; replaying a fixture
needs only the sanitized files.

Each numbered JSON file is one original payload with private values replaced.
Keys, nesting, scalar types, empty strings, booleans and event names are
preserved. `expect.tsv` records, in capture order:

```
elapsed_ms  payload  --event  state  title  subagent
```

Columns are separated by tabs. `-` in the event column means the event comes
from the payload; `-` in the title column means empty. `absent` means the session
was removed. Subagent is `0` or `1`. Time comes from hook reception, not the
payload's timestamp: Qwen's idle notification precedes StopFailure by 16/14 ms.
Nonempty prompts are replaced with distinct `Sample prompt N` values so later
submissions still test that the first title remains. Empty prompts remain empty.

`test_agent_fixtures.c` calls `agent_hook_run`, wraps only `control_request`,
and uses the production event request parser, session application and title/cwd
handlers. It checks session existence, own and resolved state, title, and child
marker after every step. A separate process isolates each scenario's stdio and
signal changes. No real agent, Wayland compositor or desktop socket is used.

Antigravity's `cancel_hook_gap` deliberately checks only the three later
PreInvocation hooks in the recording. There is no cancellation hook/log line
in that file. The intervening user cancellation, waiting state and idle
transition **cannot** be tested from this hook capture. Do not synthesize them.
No fixture contains a child-agent event; `subagent=0` checks absence only.

Run `make test-agent-fixtures` for focused offline checks; both checks are also
part of `make test`. Add new recordings by preserving each complete sequence,
writing independent expectations, supplying provenance, and updating the
manifest. Never infer a recording version from whichever agent is installed.

`python3 scripts/check_agent_versions.py` compares, for every agent, the
version its hooks were last checked against with the latest public release:
the fixtures' recorded versions, or `validated_version` in the manifest (the
version `docs/agents.md` names) while an agent has no fixtures. The manifest
lists each lookup and where its package name comes from. Grok, Cursor Agent
and Antigravity CLI have no public registry listing and are reported as such.
`.github/workflows/weekly.yml` runs it every Monday and warns on a newer
release. After observing a new release, record fixtures for it or raise
`validated_version`. The files under `tests/agent_version_responses/` are
hand-written examples of each registry's response format for the offline
parsing test, not recordings.

## Automated revalidation

Four recipes under `scripts/agent_recipes/` drive Claude Code, Codex, Copilot
CLI and Pi. They declare commands, configuration locations, version and update
commands, screen patterns, keys, independent state expectations and explicit
skip reasons. Other adapters can be added with a recipe: JSON/TOML command
hooks and PATH-resolved JavaScript/TypeScript bridges are supported. An absolute
hook command is rejected rather than forwarded or rewritten in the user's files.

```sh
make agents-check
python3 scripts/record_agent_hooks.py codex --scenario normal --out /tmp/hooks
python3 scripts/judge_agent_recording.py /tmp/hooks/normal \
  --against tests/agent_fixtures/codex/normal
python3 scripts/update_agents.py --no-upgrade --accept --force --out /tmp/current
make agents-update
```

`--check` only reports actual installed versions. Matching validated versions
are skipped, so use `--force` to deliberately upgrade and revalidate an already
validated installation. Without `--accept`, recordings and Markdown judgements
stay in a new private `/tmp` output directory (or `--out`). Output scenario
directories must be empty. Live recordings are never run by CI.

A private `tmux -L herdcat-record-<pid>` server runs each agent in a new empty
`/tmp` project with a `.git` boundary. A PATH shim stores exact stdin and argv,
returns the expected policy response, and never invokes installed herdcat.
Necessary login/configuration files are copied into a temporary home; originals
are never edited. Copilot can use the existing GitHub CLI credential in memory.
Codex has its own temporary `CODEX_HOME`, no shared daemon and vetted temporary
hooks whose trust is bypassed only for that invocation. The recorder verifies
its first real hook, applies bounded waits and retains sanitized failure screens.
Shutdown targets only its dedicated server and processes tagged with its private
log path, with PID start-time checks before signalling. Raw logs and temporary
credentials are removed; sanitized fixtures retain the raw log checksum and
source line numbers.

The existing six-column `expect.tsv` format now also allows `-` in the payload
column: this is an authored checkpoint, with **no injected hook**. Corresponding
labels and times are in `provenance.json.milestones`. Each numbered payload still
has its original source line and argv in `provenance.json.steps`. The scenario
ends at its final checkpoint; hooks generated only during cleanup are excluded.
Nonempty content and private identities are redacted without changing keys,
nesting, types, empty strings, event names or reception times. Historical Pi
process IDs are rebound to the live replay parent in the test harness; malformed
PID types are still rejected by the real hook path.

The judge uses `build/test_agent_fixtures --dump` to enter production
`agent_hook_run`, the production control decoder and session handlers, without
Wayland or a desktop socket. `--adapter` exports the compiled adapter's aliases
and rule names, avoiding a second consumed-field list. It compares event order
patterns and per-event key-path/type sets, and checks every declared state,
title and child marker. Judgements are `unchanged`, `compatible`, `new` when no
baseline exists, or `needs-attention`; the latter exits nonzero. Skipped scenes
are reported separately and do not manufacture payloads.

Acceptance checks each safe scene, replaces only those scene directories and
runs `make test-agent-fixtures`. Failure restores that agent and the manifest.
A scene marked `needs-attention` is never replaced. If any scene fails, safe
scenes may still be accepted, but the whole-agent `validated_version` is retained
and the manifest explicitly records partial validation. The overall command
still exits nonzero. Neither script commits or pushes. Hook-only replay cannot
validate transcript-based interruption, terminal quiet detection or live signs.
