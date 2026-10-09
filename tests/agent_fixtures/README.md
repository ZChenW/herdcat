# Recorded agent hook fixtures

Only attributable payloads recorded from a real agent belong here. Do not
reconstruct missing payloads from event tables, adapter rules, or synthetic
runtime tests. `manifest.json` inventories all ten adapters; an empty scenario
list explicitly means **no raw payload available**, even when a document gives
an observation version or an event sequence.

So far there are Qwen Code 0.25.0 and Antigravity CLI 1.3.1 recordings from
2026-10-08. Every scenario has `provenance.json` with the recorded version,
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
