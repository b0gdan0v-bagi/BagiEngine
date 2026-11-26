# Entry costs recorded in the todo store only

- Version: 0.29.11
- Entry: engine-tokstats-to-cli
- Date: 2026-11-26
- Bump: patch

## Changed
- `tools/tokstats/` is removed: entry costs are recorded and read with the store's `worker.py stats` and `todo.py stats`. Adapt: run `python3 tools/todo/worker.py stats import` once on each machine to move the old local records into the store.
