# todo-classifier answers the overlap question of a queue start (wait or stack)

- Version: 0.1.20
- Entry: engine-classifier-overlap
- Date: 2026-11-16
- Bump: patch

## Changed
- `.claude/agents/todo-classifier.md` has the section "Overlap with a review branch (queue start)" and its answer `{"overlap": {"id", "decision": "wait"|"stack", "on", "reason"}}`, which `worker.py` asks when a queued entry's files meet a branch in review (BagiTodo `e5861ef`). `todo-loop` and `todo-dispatch` say what the queue start does with it.
