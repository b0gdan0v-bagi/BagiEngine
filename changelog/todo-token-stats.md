# Todo: the token cost of an entry is not recorded; only transcripts that get cleaned show it, and the conductor's share is not tied to an entry

- Version: 0.5.3
- Entry: todo-token-stats
- Date: 2026-11-19
- Bump: patch

## Added
- `tools/tokstats/tokstats.py`: the token cost of a todo entry per role (classifier, architect, developer, reviewer, conductor) from the Claude Code transcripts of a checkout and its worker folders, with its outcome (review verdicts, blocking findings by kind, `WP incomplete` runs, changed lines, plan accuracy); `record` keeps a local history that outlives the transcripts, `backfill` seeds it, `report` sums it per area and month. `todo-merge` and a stopped todo-loop task run `record`. Adapt: nothing; a game runs it as `engine/tools/tokstats/tokstats.py` from its root, which also covers its engine entries.
