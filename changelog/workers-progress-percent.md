# Workers panel: an approximate percent instead of stage/slice counts

- Version: 0.1.9
- Entry: workers-progress-percent
- Date: 2026-11-16
- Bump: patch

## Changed
- Todo Workers 0.29.0: the cards, the graph and the Todo list show progress as one approximate percent (`~45%`, rounded to 5, at most 95 until every box is ticked); the stage, slice and plan counts moved to the tooltip. An entry with a stage plan counts the whole entry; one without it reads `~45% · slice`.
- `todo-architect.md` and `todo-loop/SKILL.md`: every entry has a stage plan (the architect writes it when the entry has none, whatever its slicing; size S gets a one-stage plan from the conductor), and the conductor ticks the slice's stage box at every finish.
