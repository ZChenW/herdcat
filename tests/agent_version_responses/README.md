# Offline registry response shapes

These JSON files are **hand-authored format examples**, not captured network
responses and not evidence of any published version. Values are chosen to
exercise update detection. No test downloads anything.

- `npm.json`: npm package metadata GET
  `https://registry.npmjs.org/@qwen-code%2Fqwen-code/latest`; the response is the document selected by the `latest` dist-tag; read its
  top-level `version`. This avoids downloading the entire version history.
- `pypi.json`: PyPI JSON API GET `https://pypi.org/pypi/<project>/json`;
  `info.version`. This tests the parser only; no adapter currently uses PyPI.
- `github.json`: GitHub REST GET
  `https://api.github.com/repos/<owner>/<repo>/releases/latest`;
  `tag_name`, `draft`, `prerelease`. Drafts and prereleases are rejected.

Only numeric three-part versions with optional SemVer prerelease/build suffixes
are comparable. Unknown formats are reported as unavailable, never guessed.
The scenario manifest documents the exact queried packages/repositories.
