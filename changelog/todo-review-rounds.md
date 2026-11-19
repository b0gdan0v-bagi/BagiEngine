# todo-loop: a second review round and a remarks round re-read and re-test everything even when the fix is docs or comments only

- Version: 0.5.2
- Entry: todo-review-rounds
- Date: 2026-11-19
- Bump: patch

## Changed
- `todo-reviewer` writes each blocking finding as `[blocking:docs|comment|test|code]` with a `verify:` check; the second review round and a remarks round are scoped by what the fix range touches (`todo-loop` skill, "Scoped check"): only `*.md` or comment-only C-family changes are checked by the conductor without a reviewer agent and without a build; any other fix gets `final-r2` on the fix range only. Adapt: nothing; the game shares the skill and the agents by symlink.
