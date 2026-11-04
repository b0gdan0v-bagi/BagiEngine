---
name: todo-merge
description: Merges a reviewed todo/<id> branch (left by todo-loop in status review) into the base branch of its repository. Prints a short report of what was done, asks the owner whether to merge, and only after a yes merges with --no-ff, closes the entry (as `/todo-loop close` does), deletes the branch and, for an engine branch in a game, commits the moved pointer of engine/. Use when the user asks to merge a todo branch, accept the result of the todo loop, says "смержи ветку", "todo-merge", or wants a report before merging a todo branch.
---

# todo-merge

You are the **conductor** and do everything yourself, no agents. The skill takes one branch `todo/<branch-id>` that `todo-loop` left in status `review`, shows the owner what it holds, **asks**, and only then touches the base branch. The entry format is `todo/README.md`; the tool is `<engine>/tools/todo_sync/CLAUDE.md`; the closing steps are in `.claude/skills/todo-loop/SKILL.md` ("Closing merged entries").

`/todo-merge [<id or branch>]`

The terms `<R>` (repository of the area), `<base>`, `<todo>`, `<tracker>` and `<engine>` are the ones of `todo-loop/SKILL.md` ("Areas and the repository rules"), taken from `.claude/rules/todo-project.md`. Read that file first.

Before the answer of step 5 you change nothing: only reads, `git -C <R> switch <base>` (the one change to the checkout; step 5 undoes it on `No`) and `git merge-tree`.

## Steps

1. **Preconditions.** Every area repository is clean (`git -C <R> status --porcelain`; in a game the game repository with `--ignore-submodules=all`) and `<engine>/.venv/bin/python` exists; otherwise stop with a message. Remember the starting branch of every area repository. Never `fetch` and never `push`.
2. **Choose the branch.** `git -C <R> switch <base>` for each area. Run `<engine>/.venv/bin/python <engine>/tools/todo_sync/todo_sync.py --todo <todo> parse` per area. Candidates: branches of `git -C <R> branch --list 'todo/*' --no-merged <base>` whose entry has a `status` that starts with `review` and names that branch (`branch todo/<branch-id>`). Unmerged branches that are not candidates (for example `in progress ... stopped`) are only named, with the status.
   - With an argument (`<id>` or `todo/<branch-id>`): take that branch. If it is not a candidate, stop and say why: no such branch, already merged into its base, or the status is not `review`.
   - Without an argument: one candidate is taken as is; several candidates go to the owner in one AskUserQuestion (area, title, branch, the one line of the status); none: say "nothing to merge" and stop.
3. **Dry check.** `git -C <R> merge-tree --write-tree <base> <branch>`: a non-zero exit code means conflicts, so report the files and stop (the skill never resolves conflicts). Compare `git -C <R> merge-base <base> <branch>` with the tip of `<base>`: if they differ, "the base moved since the branch was cut" is a flag for the report and for step 6.
4. **Report.** In Russian, short, printed before the question:
   - the title of the entry, the area and the branch;
   - if the status text has `slice K of M, after the merge set the status back to open`: a warning that the merge does not finish the entry (closing sets it back to `open` instead of deleting it);
   - what was done and how it was checked: the one line from `status: review ...`;
   - `git -C <R> log --oneline <base>..<branch>` and `git -C <R> diff --stat <base>...<branch>`, plus two to four lines of your own on what changed, from the commit subjects and a look at the diff of the non-test files;
   - from the slice plan `~/.claude/plans/todo-loop-<branch-id>.md`, if it exists: "Review notes" (non-blocking findings) and what the architect left for the owner to check by hand;
   - the `merge-tree` result (clean) and the "base moved" flag;
   - for an engine branch in a game: that the answer also makes a separate commit in the game repository that moves the pointer of `engine/`.
5. **Ask.** One AskUserQuestion: `Merge + close + delete branch` (for an engine branch in a game: `+ move the pointer of engine/`), `Merge only (no close, the branch stays)`, `No`. `No` ends the skill with nothing changed: switch every area repository back to its starting branch first, so the owner stays on the branch they were trying.
6. **Merge.** `git -C <R> merge --no-ff -m "Merge branch '<branch>'"` on `<base>` with the Co-Authored-By line the conversation gives (the history uses this form). If the merge fails for any reason: `git -C <R> merge --abort`, switch back to the starting branches, report, stop. If the base had moved (step 3), also build with the preset of `build` and run `ctest --preset <p>` on the result (the targets of the rules file); red tests: stop before step 7, name the failing tests, leave the merge commit (it is local; no reset, the owner decides).
7. **Close** (only for the full answer). Do steps 1 to 6 of "Closing merged entries" in `todo-loop/SKILL.md`, as written there, for this area. Narrowing: only the entry of this branch; other entries that its step 2 would find are named in the report and left alone. Without Vikunja settings the steps for `pull` and `push` are skipped and the report says so; `--project <tracker>` is always passed.
8. **Delete the branch** (only for the full answer, after step 7, because closing finds entries through `git branch --merged <base>`): `git -C <R> branch -d <branch>`. On failure report it and leave the branch.
9. **Move the pointer** (only for the full answer and only for an entry of the engine area in a game). `engine/` now stands on the merged `<base>` and carries the close commit. In the game repository: `git add engine` and one commit `Update engine: <title>` with the Co-Authored-By line the conversation gives, on the branch the game repository stands on (it is the owner's own; if that is not its base branch, say so in the report and still commit only the pointer). Nothing else is staged. The pointer is never moved before the merge, and never to a commit that is not in `<base>`.
10. **Report the result.** In Russian: the merge commit, the close commit, the pointer commit, the output of `pull` and `push`, files left uncommitted by `pull --apply` (the owner commits them), whether the branch was deleted, whether build and tests ran, and where the checkout stands now (the base branch, since the work branch is gone). End by saying that `git push` is the owner's.

## Forbidden

`git push`, `fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; resolving conflicts; editing code or any file other than what steps 7 and 9 edit; merging a branch that is not in status `review`; any change before the answer of step 5.
