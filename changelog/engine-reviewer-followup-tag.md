# The engine's reviewer does not always mark notes [followup], so the merge loses them

- Version: 0.6.8
- Entry: engine-reviewer-followup-tag
- Date: 2026-11-20
- Bump: patch

## Changed
- The loop's reviewer (`.claude/agents/todo-reviewer.md`) tags every non-blocking finding `[followup]` or `[note]`; `todo-loop` step 5 tags an untagged one itself and stores `-review-final.md` for every slice, so `/todo-merge` no longer loses follow-ups.
  Adapt: nothing; a game links these files and gets the change with the engine.
