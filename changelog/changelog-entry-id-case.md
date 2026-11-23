# changelog.py refuses the entry id of an alltests entry (upper-case letters: alltests-BagiHeadersStandalone)

- Version: 0.10.3
- Entry: changelog-entry-id-case
- Date: 2026-11-23
- Bump: patch

## Changed
- `changelog.py` takes an `Entry` id with upper-case letters (the `alltests-<ctest name>` entries); an underscore and the rest stay refused. `check` names the version header in posix form on every platform, and the tool's tests pass on Windows.
  Adapt: nothing.
