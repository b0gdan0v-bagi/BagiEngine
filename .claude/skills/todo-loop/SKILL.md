---
name: todo-loop
description: Works through the entries of todo/ one by one, each in its own local branch todo/<id> cut from the base branch of its repository, with a classifier, an architect, developers and a reviewer as separate agents, and leaves the last finished branch checked out in status review so the owner can try it live. With `close` it finishes merged branches (deletes the entry, syncs the tracker). Use when the user asks to run the todo loop, work through the todo list on its own, take the next todo entry, list what the loop could take, or says a todo branch is merged and its entry should be closed.
---

# todo-loop

You are the **conductor**. You do not read or write code: you run git, edit the status in the front matter of one entry file, start agents (`.claude/agents/todo-*.md`), and keep short reports in your context. All work on code happens in the agents. The entry format is `todo/README.md`; the tool is `<engine>/tools/todo_sync/CLAUDE.md`.

State lives in git (a branch `todo/<id>*` that is not merged into its base branch means the entry is taken), in the entry file and in the slice plan, so the loop can be stopped and started again on this machine.

## Areas and the repository rules

Read `.claude/rules/todo-project.md` of the repository you run in before anything else. It holds the **areas**: where entries live and which repository, base branch and tracker project each one uses. This skill is the same text in the engine repository (one area) and in a game that carries the engine as the submodule `engine/` (two areas: the game and the engine).

Terms used below, taken from the row of the entry's area:

| Term | Meaning |
|------|---------|
| `<R>` | the git repository of the area (`.` or `engine`). Every git command for an entry of that area runs as `git -C <R> ...`; its branch `todo/<branch-id>` lives there. |
| `<base>` | the base branch of `<R>` (`main`, or `games/chess` for the engine inside a game). Branches are cut from it and merged into it. |
| `<todo>` | the todo folder of the area; the tool runs with `--todo <todo>`. |
| `<tracker>` | the Vikunja project of the area. `pull` and `push` always get `--project <tracker>` (and `--env-file` when the row names one); never rely on the tool's default. |
| `<engine>` | `.` in the engine repository, `engine` in a game: the folder with `.venv`, `tools/` and `CI/`. The Python is `<engine>/.venv/bin/python`, the tool `<engine>/tools/todo_sync/todo_sync.py`. |

The rules file also names the build and test targets (baseline and reviewer), the files the agents read first, the zones nobody edits, and the skip reasons and examples of the classifier. Agents are told to read it too.

## Arguments (all optional)

`/todo-loop [N] [<id>] [list] [plan-only] [close] [models=role:model,...]`

| Argument | Effect |
|----------|--------|
| `N` | Take at most N slices. Default 1. |
| `<id>` | Work on this entry. If an unmerged branch `todo/<id>*` exists, continue it from the first WP whose boxes are not ticked. Ids are unique across areas; on a clash write `<area>:<id>`. |
| `list` | Only the classifier runs. No branches, no edits. |
| `plan-only` | Classifier and architect. The branch holds one commit (the status); the plan stays in the plan store. |
| `close` | After the owner has merged branches: close their entries and sync the tracker (section "Closing merged entries"). Nothing else runs. |
| `models=classifier:haiku,reviewer:sonnet` | Override the model of a role for this run. `haiku`, `sonnet` and `opus` are allowed; `opus` is the default of the reviewer only, the other roles stay at or below `sonnet`. |

## Roles and models

| Role | Agent | Model | Does |
|------|-------|-------|------|
| Classifier | `todo-classifier` | haiku | Chooses the next eligible entry, answers JSON. |
| Architect | `todo-architect` | sonnet | Cuts the slice into work packages (WP), writes `~/.claude/plans/todo-loop-<branch-id>.md`, may answer `BLOCKED`. |
| Developer | `todo-developer` | sonnet | One WP: code, tests, docs, format, commit `WP k/M: ...`. A new agent for every WP. |
| Reviewer | `todo-reviewer` | opus | Fresh context, reads the diff, builds and runs the tests, answers `APPROVE` or `CHANGES`. |

Start agents with the Agent tool and `subagent_type` set to the agent's name, one at a time, in the foreground (the checkout is shared, and a background agent cannot ask for permissions). Never use SendMessage to a developer that has finished: each WP gets a new one. Every agent gets the area of the entry (`<R>`, `<base>`, `<todo>`) in its task. Size **S** (from the classifier): skip the architect and give the developer the entry as one WP, with no plan file.

## Preconditions

Stop with a message when any of these fails (for `list` and `close` only the first, the venv and the plan store are checked, not the build):

- Every area repository is clean: `git -C <R> status --porcelain` is empty for each `<R>`. In a game the check of the game repository adds `--ignore-submodules=all`: the engine on a work branch of an earlier run shows there as a changed pointer, by design. (The loop works in this checkout; the owner must not edit files while it runs.)
- `<engine>/.venv/bin/python` exists (`CI/setup_venv.*`), the submodules and `<engine>/external/googletest` are initialized (`git submodule update --init --recursive`), the preset of this OS is in `cmake --list-presets=build`.
- `~/.claude/plans/` exists and can be written.

Not a stop, a flag: `test -f ~/.claude/skills/kovalev-review-checklist/SKILL.md`. Pass the reviewer `checklist: available` or `checklist: unavailable` accordingly.

Then remember the **starting branch of every area repository**, `git -C <R> switch <base>` in each, and once per run build the targets of the rules file and run `ctest --preset <p>`. The names of the failing tests go to a file in the scratchpad: the **baseline**.

## The loop

1. **Choose.** `git -C <R> switch <base>` for every area. First look for entries that wait to be closed (see "Closing merged entries", step 2). If there are any, ask the owner once (AskUserQuestion): close them now and then continue, or continue without closing. Run `close` only on a "close" answer; never on your own. For every area run `<engine>/.venv/bin/python <engine>/tools/todo_sync/todo_sync.py --todo <todo> check` and `parse`. Taken ids: `git -C <R> branch --list 'todo/*' --no-merged <base>` (a merged branch does not take an entry; strip `todo/` and a `-K` suffix). Candidates from all areas: entries with `status == "open"` and no taken id, ordered: no stage first, then stages 1 to 6, then by `id`. Entries `in progress` are the owner's; the loop never takes them. Give the classifier the candidates (`area`, `id`, `title`, `stage`, `summary`, `file`). Check its `next` yourself: the entry is in `parse`, its status is `open`, no taken branch. With `<id>`, skip the classifier unless `list`. No eligible entry: stop the loop.
2. **Branch.** `git -C <R> switch -c todo/<id> <base>` (`todo/<id>-2`, `-3` for the next slice of an entry whose earlier branch is merged: the first free name). First commit (in `<R>`) changes only the front matter of `<todo>/<id>.md`: `status: in progress <YYYY-MM-DD> branch todo/<branch-id>` (create the front matter if the entry has none; keep `stage:` and `plan:`).
3. **Plan.** Start the architect (not for size S) with the entry, area, branch, size and the `plan`, `planFound`, `planProgress` of the entry. `BLOCKED` goes to "Stopping a task". The plan file is not committed, and the entry's `plan:` line is not changed (it points to the owner's plan; the slice plan is a separate file). With `plan-only`, end the task here with the status commit and go to step 7.
4. **Implement, WP by WP.** For each WP a new developer, with the WP text, the plan path, the area and the branch. If it ends with `WP incomplete: ...`, start a new developer for the rest (at most 2 times; then the architect cuts again or the task stops).
5. **Review, WP by WP.** After each WP a new reviewer, with the commit range of that WP (in `<R>`), the plan path, the WP text, the area, the baseline file and the checklist flag. `CHANGES` with blocking findings: a new developer gets the findings, then a new review; at most 2 rounds per WP. Non-blocking findings go to "Review notes" in the plan file. After the last WP, when there is more than one, one integration pass: a reviewer on `<base>...HEAD` (diff stat, full build and `ctest`, is the plan closed, are the docs and the entry consistent).
6. **Finish.** In the front matter of `<todo>/<id>.md`: `status: review <YYYY-MM-DD> branch todo/<branch-id>: <one line: what was done, how it was checked>`. If the slice does not close the entry, append `, slice K of M, after the merge set the status back to open`. The body of the entry stays. Run `check`; commit `Mark <title> ready for review` (with the Co-Authored-By line the conversation gives). The plan file stays in the store, and its path goes to the report. Remember the branch and `<R>` as the **last review branch** of the run.
7. **Next.** `git -C <R> switch <base>` and go to step 1 until a stop condition. The work branch stays checked out only at the end (see "End of the run").

The edits to the front matter are the only edits you make to a file. Everything else is the agents'.

## End of the run

The owner wants to try a change live before merging, so the run does not end on the base branch:

- If at least one slice of this run ended in status `review`, switch the repository of the **last review branch** to it: `git -C <R> switch todo/<branch-id>`, and stay. Every other area repository goes back to its starting branch. For an engine slice in a game, only `engine/` moves; the game repository stays where it started and builds with the engine on the work branch (it shows `engine` as a changed pointer, which the precondition above allows).
- Otherwise (`list`, nothing taken, every task stopped, `close`) every repository goes back to its starting branch; `close` ends on the base branch, where it committed.
- A branch that stopped as `in progress ... stopped` is never the one to stay on.
- The tree must be clean for the switch (the finish commit guarantees it). Never `stash` or `reset` to get there; if the switch fails, say so in the report and leave the repository on `<base>`.

## Closing merged entries (`close`)

The owner starts it after merging (or answers "close" to the offer in step 1). It is the only mode that commits on a base branch, runs `git rm` on an entry file and calls the tracker. No agents: you do it yourself. It works area by area.

1. Preconditions: the repositories are clean (as above), `<engine>/.venv/bin/python` exists. `git -C <R> switch <base>`. Without Vikunja settings (`VIKUNJA_URL`, `VIKUNJA_TOKEN` in the `.env` the area names, or in the environment), steps 5 and 6 are skipped and the report says so.
2. Find the entries: for each branch of `git -C <R> branch --list 'todo/*' --merged <base>`, the entry in `parse` whose `status` starts with `review`, names that branch (`branch todo/<branch-id>`) and whose file still exists. An entry in another status (for example `in progress ... stopped`) is only named in the report. None found: say "nothing to close" and stop, changing nothing.
3. Per entry: if the status text has `slice K of M, after the merge set the status back to open`, remove the `status:` line from the front matter (keep `stage:`, `plan:`, `id:`; drop the `---` block when it is left empty) and leave the body alone. Otherwise `git -C <R> rm <todo>/<file>`.
4. `todo_sync.py --todo <todo> check`, then one commit in `<R>` on `<base>`: `Close <titles>` (with the Co-Authored-By line the conversation gives).
5. `todo_sync.py --todo <todo> pull --project <tracker>` (add the area's `--env-file`; report only); show it.
6. `pull --apply`, then `push`, with the same `--todo`, `--project` and `--env-file`. Stop at the first failing command and report it; no retries. Files that `pull --apply` deleted or adopted stay uncommitted: name them in the report for the owner to commit.

For the engine area in a game, the close commit moves the engine, so the game repository shows a new pointer: `close` does not commit it, the report names it (`todo-merge` makes that commit, after its own yes).

Never in `close`: git `push`, `fetch`, `branch -d` or `-D`, or any entry other than the ones found in step 2.

## Stopping a task (status stays `in progress`)

Commit what exists, set `status: in progress <date> branch todo/<branch-id>, stopped: <reason>`, `git -C <R> switch <base>`, take the next entry. Reasons: `BLOCKED` from the architect; the same failure after 3 attempts; 2 review rounds without `APPROVE`; a WP not closed after 2 restarts of the developer; the work grew beyond the entry.

## Stopping the whole loop

No eligible entry; N slices done; the baseline on a base branch is red; a working tree is dirty; the same environment failure (permissions, missing toolchain) on two tasks in a row. The repositories end as described in "End of the run".

## Forbidden (also written into the agents)

`git push`, `git fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; editing a base branch; editing the zones that the rules file lists (vendored code); editing outside the entry and its WPs; `git rm` of an entry file; any `todo_sync.py push` or `pull` (a normal run makes no network call and does not touch the tracker); moving the pointer of a submodule in the game repository; putting Homebrew LLVM on `PATH` for `cmake --preset`. The exceptions are the `close` mode, started by the owner after a merge, and the `todo-merge` skill (merge into the base branch, `branch -d` of the merged branch, the pointer commit, only after the owner's yes), which runs the same closing steps: there the conductor may commit on a base branch, `git rm` the entry files it found and run `pull` and `push`. The agents keep the whole list, always.

## Report

For the owner, in Russian, short. First line: **on which branch of which repository the checkout stands now** (`todo/<branch-id>` in `<R>`), how to check it live (build with the `build` skill: after the switch a rebuild is expected; how to start the game; the manual items the architect left in the plan), and the other branches of this run (`git -C <R> switch todo/<other>`). Then a table "entry, area, branch, commits, build/tests, reviewer verdict, plan path"; the skipped entries with reasons; what was not checked (platforms, manual items that the architect left for the owner, the checklist when it was unavailable). End with the owner's queue: for each area `git -C <R> branch --list 'todo/*' --no-merged <base>`, and the offer: run `/todo-merge` (report, ask, merge, close, delete the branch; it switches to the base branch itself), or merge by hand and run `/todo-loop close`, which deletes the entry file (for a slice, sets the status back to `open`), then runs `pull --apply` and `push`. For `close` itself the report names the closed entries, the commit, the output of `pull` and `push`, the pointer that the owner still has to commit, and the files left uncommitted.
