# Engine tools work without the todo store

- Version: 0.29.12
- Entry: engine-tools-without-store
- Date: 2026-11-26
- Bump: patch

## Changed
- `perf` measures without the todo store (no baseline comparison then), and `alltests` keeps its result and drafts its failure entries locally when the store is absent, refuses or hangs; a later run with the store files them. What each tool does without the store is in `tools/todo/CLAUDE.md` and `NEW_GAME.md`.
