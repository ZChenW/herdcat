# Setup fixtures

These are hand-built examples, never copied from real agent configuration.
Every credential sentinel is `sk-FAKE-DO-NOT-USE`. Tests copy examples into a
fresh temporary HOME with every XDG directory redirected there.

Each agent has `blank`, `other` and `old` examples. JSON examples exercise
nested command groups (Claude, Codex, Grok), direct commands (Cursor), and
`bash`/`timeoutSec` (Copilot). The old JSON/TOML examples include both herdcat
and bongocat command prefixes. Pi's `other.ts` is a separate `audit.ts`, not
an unrelated file occupying herdcat's private extension path. Blank Pi means
no extension. opencode's old plugin path uses `@HERDCAT_PLUGIN@`, replaced
only with the temporary HOME's private bridge directory; `old-index.js` is
its obsolete bridge. Comments and trailing commas are intentional.

`scripts/test_setup.py` compares complete installed bytes against expected
configurations derived from these examples and the committed integration
templates. It also compares complete removed bytes against the original
examples, or the examples with obsolete managed entries removed. It never
executes hook commands or loads an agent, and requires no credentials.

Evidence and version differences are recorded locally in
`docs/performance/stage11.md`.
