# Remove plans from the repository; keep live ones in the local Claude store

## Context

`.claude/plans/` holds 20 tracked/untracked plans plus an `archive/` with 2 more. Sixteen are finished work. The owner's decision:
plans do not belong in the repo. Finished ones are deleted (git history keeps them); the plans still in progress live only in the
local Claude store on this PC (`~/.claude/plans/`, Claude Code's default) and are deleted by hand when their work completes.

Why a plain delete is not enough: `.claude/settings.json` sets `"plansDirectory": "./.claude/plans"`, so every new plan would
land in the repo again. And two live plans exist only in the working tree (`delightful-stirring-eagle.md` is untracked,
`magical-leaping-blossom.md` has uncommitted edits), so deleting them without copying loses them for good; git cannot restore them.

No code, no build, no commit.

## Steps

1. **Copy the 4 live plans out first** (no name collisions in `~/.claude/plans/`, checked):
   `buzzing-tinkering-cat.md`, `chess-staged-stream.md`, `magical-leaping-blossom.md`, `delightful-stirring-eagle.md`.
   Use `cp` and compare with `cmp` before anything is removed. The plans mention each other by bare file name, which still works in one folder.
2. **Delete the plans from the repo**: `git rm -r .claude/plans` for the tracked files (16 finished, 2 in `archive/`, and the tracked
   live ones), `rm` for the untracked `delightful-stirring-eagle.md`. This plan file goes last, since it is the file being executed.
   The finished ones: abstract-growing-honey, chess-mvp-module, claude-rules-fuzzy-emerson, clang-format-agile-panda,
   cmake-presets-wobbly-mist, effervescent-churning-crown, game-only-run-mode, linked-sparking-cake, match3_mvp_module,
   parsed-painting-toast, quirky-frolicking-catmull, scene-view-fit, task-handle-lifetime, todo-fizzy-cloud, todo-soft-nygaard,
   typed-jumping-toast. Nothing outside `.claude/plans/` links to any of them (grep over the repo).
3. **Stop plans from coming back into the repo**:
   - `.claude/settings.json`: remove the `plansDirectory` line (the file would be `{}`, so delete the file instead; it has no other keys).
   - `.gitignore`: add `.claude/plans/`.
4. **Fix the docs that describe the old rule**:
   - `CLAUDE.md:155` ("save a plan to `.claude/plans/PLAN_NAME.md` (plan mode writes there via `plansDirectory`)"): say that plan mode
     writes plans to the local Claude store, they are not committed, and the owner deletes a plan when its work is done. Keep the
     "include testing and documentation tasks / end with clang-format and strip-pch-includes" part as it is.
   - `src/Modules/BECore/Control/CLAUDE.md:5`: drop "and `.claude/plans/`" (the stages stay described in `TODO.md`).
5. **The one decision with a side effect: `TODO.md` and `todo_sync`.** The "No external control" entry has
   `**Plan:** .claude/plans/buzzing-tinkering-cat.md`, and `tools/todo_sync` reads that plan's `- [x]` / `- [ ]` lines for the
   dashboard progress. A plan outside the repo cannot be read from there (and not on another machine). Recommended: remove that
   `**Plan:**` line from the entry and the `**Plan:**` bullet from the field list in the `TODO.md` header (line 9). The entry's
   `**Status:**` text already says which stages are done. The `todo_sync` code is not touched: with no `**Plan:**` line it already
   reports no progress (`planProgress: null`, same as the other entries). If you would rather keep dashboard progress, the
   alternative is to keep that one file in the repo, which goes against "all plans out"; say so at approval.

## Verification

- `ls .claude/plans` fails (folder gone); `git status --short` shows only deletions under `.claude/plans/`, the edits to
  `.gitignore`, `CLAUDE.md`, `src/Modules/BECore/Control/CLAUDE.md`, `TODO.md`, and the removed `.claude/settings.json`; the
  uncommitted Chess sources and `TODO.md`-unrelated edits are untouched.
- `cmp` of each of the 4 plans against its copy in `~/.claude/plans/` was clean before the delete (step 1).
- `python3 tools/todo_sync/todo_sync.py check` exits 0 and prints no warning about `**Plan:**`.
- `grep -rn "\.claude/plans" CLAUDE.md TODO.md src games tools .claude` finds nothing that still treats the folder as part of the repo
  (the `todo_sync` parser docstring and its tests keep the string; that is the tool's generic support for a plan path).
- No commit is made; the owner commits when ready.
