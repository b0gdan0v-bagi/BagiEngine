# Todo workers stand idle until the owner acts: no self-picking of the next entry, a blocked entry pauses the queue

- Version: 0.5.3
- Entry: workers-self-pick
- Date: 2026-11-18
- Bump: patch

## Added
- `todo-loop` can pick the next entry at the end of a run (`/todo-loop 1 auto`), only from an allowlist of areas and id prefixes the owner sets. It is off without that setting, and it holds to at most 3 branches in review, a daily cost cap and a stop after two parked entries in a row.

## Changed
- A blocked entry is parked and the worker goes on, a question for the owner goes to one list with a recommendation, and a worker makes small reversible decisions itself and lists them in its review report.
- A branch goes to review only when it holds the commits of the slice.
- An entry whose files meet one branch in review stacks on it or waits, and `todo-merge` sends a merge conflict back to the worker as a conflict round.
