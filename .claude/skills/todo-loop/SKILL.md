---
name: todo-loop
description: Works through the entries of the todo store one by one, each in its own local branch todo/<id> cut from the base branch of its repository, with a classifier, an architect, developers and a reviewer as separate agents, and leaves the last finished branch checked out in status review so the owner can try it live. With `close` it finishes merged branches (closes the entry in the store). Use when the user asks to run the todo loop, work through the todo list on its own, take the next todo entry, list what the loop could take, or says a todo branch is merged and its entry should be closed.
---

# todo-loop

You are the **conductor**. You do not read or write code: you run git, move the status of one entry in the todo store, keep its plan and reviews in the store, start agents (`.claude/agents/todo-*.md`), and keep short reports in your context. All work on code happens in the agents. The store and its CLI are described in `<engine>/tools/todo/CLAUDE.md`.

State lives in the **todo store** (the status of an entry: `in progress` with its branch means taken; its slice plan and reviews) and in git (the branch `todo/<id>*` with the work), so the loop can be stopped and started again on any machine. **Nothing about the state of an entry is committed**: no status commits, no entry files, no plans.

## Areas and the repository rules

Read `.claude/rules/todo-project.md` of the repository you run in before anything else. It holds the **areas**: which store area, repository and base branch each one uses. This skill is the same text in the engine repository (one area) and in a game that carries the engine as the submodule `engine/` (two areas: the game and the engine).

Terms used below, taken from the row of the entry's area:

| Term | Meaning |
|------|---------|
| `<area>` | the id of the area in the store (`engine`, `chess`, ...); every entry has one. |
| `<R>` | the git repository of the area (`.` or `engine`). Every git command for an entry of that area runs as `git -C <R> ...`; its branch `todo/<branch-id>` lives there. |
| `<base>` | the base branch of `<R>` (`main`, or `games/chess` for the engine inside a game). Branches are cut from it and merged into it. |
| `<engine>` | `.` in the engine repository, `engine` in a game: the folder with `tools/` and `CI/`. |
| `todo` | short for `python3 <engine>/tools/todo/todo.py --actor todo-loop` (standard library only; `TODO_API_URL` and `TODO_API_TOKEN` come from the git-ignored `.env` of the repository). |

The rules file also names the build and test targets (baseline and reviewer), the files the agents read first, the zones nobody edits, the skip reasons and examples of the classifier, and the workers (below). Agents are told to read it too.

**Only the areas of the rules file.** The store is shared: it holds the entries of every game (`chess`, `mahjong`, ...) and of the engine, and loops of several repositories (and machines) read it at once. A loop works **only the areas that are rows of its rules file**: every `todo list` of this skill names them, one call per area (`todo list --area <area> ...`; the store takes one area per call), and is never run without `--area`. An entry of another area is not a candidate, not a busy entry, not claimed, not closed and not named in the report. The `engine` area is a row of every game's rules file, so any game may take an engine entry; it is worked from that game's `engine/` and its `<base>` (`games/<game>`), and the store's `claim` keeps two loops off it. An engine entry in `in progress` or `review` whose branch does not exist in this `<R>` was taken in another checkout (another game or machine): its busy files are only the `## Files` of its slice plan, and `close` leaves it alone.

## Parallel workers

Several loops can run at once, one Claude session each, in **worker folders**: git worktrees of the repository made by `python3 <engine>/tools/todo/worker.py create <N>` (`<engine>/tools/todo/CLAUDE.md`, "Workers"), in `<parent>/<name>.workers/w1` ... `wN`. They share the one `.git` (in a game the engine module too), so every branch `todo/*` is visible in every folder and in the main checkout, where the owner tries branches and runs `/todo-merge`. The main checkout is the owner's; the loops run in the workers (`/todo-loop-parallel`, through `worker.py start`, starts one background session per worker, a minute apart). Each loop is this skill, run in its own folder, and the store's `claim` makes sure two of them never take the same entry (HTTP 409 for the second). What the workers add:

- **Go to the base** means `git -C <R> switch <base>`, or `git -C <R> switch --detach <base>` when `<base>` is checked out in another worktree (`git -C <R> worktree list`): a branch can be checked out in one worktree only, and the main checkout holds the base branches. Wherever this skill says "go to the base", it means this.
- **Busy files.** Two branches that change the same file conflict at the merge, and `todo-merge` does not resolve conflicts, so a loop does not take an entry that would change what another taken entry changes (steps 1 and 2). The busy files of a taken entry (`in progress` or `review`, with a branch) are what its branch changed, `git -C <R> diff --name-only <base>...<branch>`, plus the `## Files` section of its slice plan (`todo plan get todo-loop-<branch-id>.md --stdout`, when the store has it). Entries of different areas never meet (different repositories). Two paths meet when they are equal or one is a folder (ending with `/`) that holds the other. The shared files of the rules file (section "Workers") do not count alone.
- In a worker, never run `git submodule update` in the game repository: the worker's `engine/` is a worktree of the main checkout's engine module, `worker.py` turned the update off for the worker, and `worker.py check` says whether the shared config is still right.
- **Reports to the launcher.** With `report-to=<session>` the loop sends its news to that session with `SendMessage` (`to`: the name as given), so the owner reads every worker in one place. A message is plain text, in Russian, its first line `w<k> · <id>: <what happened>` (`w<k>` is the worker folder's name, or `main` outside a worker), at most about 15 lines, with the absolute links of the chat (plan, reviews, pages in the store). Send one when: a slice reaches `review` (step 6: branch, repository, commits, build/tests, reviewer verdict, the note), a task stops ("Stopping a task": the reason), the loop stops with nothing taken or on an error ("Stopping the whole loop"), a question waits for the owner (the AskUserQuestion of step 1: the message says the worker waits and how to answer, `claude attach todo-w<k>`), and at the end of the run (the report of section "Report", shortened to the first line, the table and the owner's queue). A send that fails (the session is gone, it refused) is a note in the own report; the loop does not stop or retry for it. Never send anything else: no progress messages per WP, no questions to the launcher, no instructions.
- Start the sessions a minute or so apart: two loops that choose at the same moment can still take two entries that meet (the window is the time between the check of step 2 and the stub plan).

## Arguments (all optional)

`/todo-loop [N] [<id>] [list] [plan-only] [close] [models=role:model,...] [report-to=<session>]`

| Argument | Effect |
|----------|--------|
| `N` | Take at most N slices. Default 1. |
| `<id>` | Work on this entry. If it is `in progress` on an unmerged branch `todo/<id>*`, continue that branch from the first WP whose boxes are not ticked. Ids are unique over every area. |
| `list` | Only the classifier runs. No branches, no writes to the store. |
| `plan-only` | Classifier and architect. The entry is claimed, the branch is cut (no commit), the plan is in the store. |
| `close` | After the owner has merged branches: close their entries in the store (section "Closing merged entries"). Nothing else runs. |
| `report-to=<session>` | Also send the reports to this Claude session (section "Reports to the launcher"). `worker.py start --report-to` passes it, so the session that started the workers hears from all of them. |
| `models=classifier:haiku,reviewer:sonnet` | Override the model of a role for this run. `haiku`, `sonnet` and `opus` are allowed; `opus` is the default of the reviewer only, the other roles stay at or below `sonnet`. |

## Roles and models

| Role | Agent | Model | Does |
|------|-------|-------|------|
| Classifier | `todo-classifier` | haiku | Chooses the next eligible entry, answers JSON. |
| Architect | `todo-architect` | sonnet | Cuts the slice into work packages (WP), writes `~/.claude/plans/todo-loop-<branch-id>.md`, may answer `BLOCKED`. |
| Developer | `todo-developer` | sonnet | One WP: code, tests, docs, format, commit `WP k/M: ...`. A new agent for every WP. |
| Reviewer | `todo-reviewer` | opus | Fresh context, reads the diff, builds and runs the tests, answers `APPROVE` or `CHANGES`. |

Start agents with the Agent tool and `subagent_type` set to the agent's name, one at a time, in the foreground (the checkout is shared, and a background agent cannot ask for permissions). Never use SendMessage to a developer that has finished: each WP gets a new one. Every agent gets the area of the entry (`<area>`, `<R>`, `<base>`, `<engine>`) in its task. Size **S** (from the classifier): skip the architect and give the developer the entry as one WP; the plan file is only the stub of step 2 (its `## Files`).

## Plans and reviews: in the store, worked on as local files

Agents read and write files, so the plan of a slice and each review have a working copy in `~/.claude/plans/` (outside every repository) and their truth in the store:

- after the architect: `todo plan put todo-loop-<branch-id>.md --entry <id> --kind slice`;
- after every developer (it ticks boxes in the local file) and after you append review notes: `todo plan put todo-loop-<branch-id>.md` again;
- every reviewer answer: save it verbatim to `~/.claude/plans/todo-loop-<branch-id>-review-<name>.md` and `todo plan put <that name> --entry <id> --kind review`;
- continuing a branch on another machine: `todo plan get todo-loop-<branch-id>.md` first.

`todo plan get` / `put` refuse a path inside a git work tree. Never write a plan, a review or an entry text into a repository.

## Preconditions

Stop with a message when any of these fails (for `list` and `close` only the first two and the plan store are checked, not the build):

- Every area repository is clean: `git -C <R> status --porcelain` is empty for each `<R>`. In a game the check of the game repository adds `--ignore-submodules=all`: the engine on a work branch of an earlier run shows there as a changed pointer, by design. (The loop works in this checkout; the owner must not edit files while it runs.)
- `todo ping` answers `ok` (the store is the state of the loop: there is no offline mode). It also shows whether the Vikunja mirror is on; a mirror error is a note for the report, not a stop.
- The submodules and `<engine>/external/googletest` are initialized (`git submodule update --init --recursive`; in a worker instead `python3 <engine>/tools/todo/worker.py check` answers `ok`, and nothing is updated), the preset of this OS is in `cmake --list-presets=build`.
- `~/.claude/plans/` exists and can be written.

Not a stop, a flag: `test -f ~/.claude/skills/kovalev-review-checklist/SKILL.md`. Pass the reviewer `checklist: available` or `checklist: unavailable` accordingly.

Then remember the **starting branch of every area repository** (in a worker that is often a detached commit; going back to it means going to the base), go to the base in each, and once per run build the targets of the rules file and run `ctest --preset <p>`. The names of the failing tests go to a file in the scratchpad: the **baseline**.

## The loop

1. **Choose.** Go to the base for every area. First look for entries that wait to be closed (see "Closing merged entries", step 2). If there are any, ask the owner once (AskUserQuestion): close them now and then continue, or continue without closing. Run `close` only on a "close" answer; never on your own. Candidates: `todo list --status open --area <area> --json` for each area of the rules file, in the order of its rows (never all areas: "Only the areas of the rules file"), ordered within an area: no stage first, then stages 1 to 6, then by `id`. Entries `in progress` are taken (by the loop, with a branch, or by the owner); the loop never takes them. As a consistency check, an unmerged branch `git -C <R> branch --list 'todo/*' --no-merged <base>` whose entry is `open` in the store is a note in the report (the owner cut it by hand); skip that entry. Give the classifier the candidates (`area`, `id`, `title`, `stage`, `summary`) and the **busy entries**: `todo list --status "in progress,review" --area <area> --json` for each area of the rules file, each with its busy files ("Parallel workers"). Take the first `eligible` entry in its order whose `touches` meet no busy file of its area (you compare the lists; the classifier's `next` does not know); name the others in the report as `conflicts with <id>: <paths>`. Check the chosen one yourself: it is in the list and `open`. With `<id>`, skip the classifier unless `list`, and take the entry even when it meets busy files (say so in the report). No eligible entry, or every eligible one conflicts: stop the loop.
2. **Claim and branch.** Right before the claim, list the busy entries again: an entry taken since step 1 (by another loop) whose busy files meet the chosen entry's `touches` means: take the next eligible one. `todo claim <id> --branch todo/<branch-id>` (`<branch-id>` is `<id>`, or `<id>-2`, `-3` for the next slice of an entry whose earlier branch is merged: the first name with no branch in `<R>`). A refusal (HTTP 409: someone else took it) means: take the next candidate. **At once** store the **stub plan**, so other loops see what this entry will change: write `~/.claude/plans/todo-loop-<branch-id>.md` with the line `# <title>: files` and a `## Files` section (the classifier's `touches`, one `- <path>` per line) and `todo plan put todo-loop-<branch-id>.md --entry <id> --kind slice`; the architect replaces it with the plan and keeps the section. Then `git -C <R> switch -c todo/<branch-id> <base>`. **No commit**: the status is in the store.
3. **Plan.** Start the architect (not for size S) with the entry id, area, branch, size, and the `plan`, `planShown`, `planProgress` of `todo show <id> --json`. `BLOCKED` goes to "Stopping a task" (no plan, no link). Otherwise store the plan (`todo plan put ... --kind slice`, above) and, before step 4 starts, write one line to the owner in the chat with links to the plan: `[Plan: <title>, slice K of M](file:///<absolute path>)` (the path absolute, the home directory expanded, never `~`; the architect answers with such a path) and the page in the store (`todo plan url todo-loop-<branch-id>.md`), plus the number of WPs. Size S keeps the stub plan and gets no link. The entry's `plan` (the owner's plan) is not changed; the slice plan is a separate plan of the entry. With `plan-only`, end the task here and go to step 7.
4. **Implement, WP by WP.** For each WP a new developer, with the WP text, the plan path, the entry id, the area and the branch. If it ends with `WP incomplete: ...`, start a new developer for the rest (at most 2 times; then the architect cuts again or the task stops). After each developer: `todo plan put todo-loop-<branch-id>.md` (its ticked boxes).
5. **Review, WP by WP.** After each WP a new reviewer, with the commit range of that WP (in `<R>`), the plan path, the WP text, the area, the baseline file and the checklist flag. `CHANGES` with blocking findings: a new developer gets the findings, then a new review; at most 2 rounds per WP. Non-blocking findings go to "Review notes" in the plan file (then `plan put`). **Every reviewer answer is stored and gets a link in the chat:** the reviewer cannot write files, so save its answer verbatim to `~/.claude/plans/todo-loop-<branch-id>-review-<name>.md` (`<name>`: `wp1`, `wp1-r2` for the second round, `integration`), `todo plan put <that name> --entry <id> --kind review`, and write one line `[Review WP k/M: APPROVE|CHANGES, <n> blocking](file:///<absolute path>)`. After the last WP, when there is more than one, one integration pass: a reviewer on `<base>...HEAD` (diff stat, full build and `ctest`, is the plan closed, are the docs and the entry consistent).
6. **Finish.** `todo review <id> --branch todo/<branch-id> --note "<one line: what was done, how it was checked>"`. If the slice does not close the entry, end the note with `, slice K of M, after the merge set the status back to open`. **No commit.** If a WP changed what the entry says (the developer did it with `todo edit`), check it with `todo show <id>`. Remember the branch and `<R>` as the **last review branch** of the run.
7. **Next.** Go to the base and go to step 1 until a stop condition. The work branch stays checked out only at the end (see "End of the run").

You write to the store (`claim`, `review`, `stop`, `close`, `plan put`) and to the plan store; you make no edit to a file in a repository. Everything else is the agents'.

## End of the run

The owner wants to try a change live before merging, so the run does not end on the base branch:

- If at least one slice of this run ended in status `review`, switch the repository of the **last review branch** to it: `git -C <R> switch todo/<branch-id>`, and stay. Every other area repository goes back to its starting branch. For an engine slice in a game, only `engine/` moves; the game repository stays where it started and builds with the engine on the work branch (it shows `engine` as a changed pointer, which the precondition above allows).
- Otherwise (`list`, nothing taken, every task stopped, `close`) every repository goes back to its starting branch.
- A branch whose entry stopped (`in progress ... stopped`) is never the one to stay on.
- The tree must be clean for the switch (the developers commit their work). Never `stash` or `reset` to get there; if the switch fails, say so in the report and leave the repository at the base.

## Closing merged entries (`close`)

The owner starts it after merging (or answers "close" to the offer in step 1). No agents: you do it yourself. It works area by area and changes nothing in git.

1. Preconditions: the repositories are clean (as above), `todo ping` answers. Go to the base.
2. Find the entries: `todo list --status review --area <area> --json` for each area of the rules file; for each entry of the area, its `branch` must be in `git -C <R> branch --list 'todo/*' --merged <base>`. An entry in review whose branch is not merged waits for the owner and is only named; one whose branch does not exist in `<R>` belongs to another checkout and is left out. An unmerged branch whose entry is in another status (for example `in progress ... stopped`) is only named.
3. Per entry: if its `statusNote` has `slice K of M, after the merge set the status back to open`, `todo close <id> --reopen` (open again, branch cleared, the body stays). Otherwise `todo close <id>` (closed; the entry stays in the store with its history, and its Vikunja task is marked done by the mirror).
4. Report the closed and reopened entries. There is no tracker step: the service mirrors the change into Vikunja by itself (`todo ping` shows the outbox; a failing one is a note).

Never in `close`: git `push`, `fetch`, `branch -d` or `-D`, a commit, or any entry other than the ones found in step 2.

## Stopping a task (status stays `in progress`)

Commit what exists (the developer's last green state), `todo stop <id> --reason "<reason>"`, go to the base, take the next entry. Reasons: `BLOCKED` from the architect; the same failure after 3 attempts; 2 review rounds without `APPROVE`; a WP not closed after 2 restarts of the developer; the work grew beyond the entry.

## Stopping the whole loop

No eligible entry (or every eligible one conflicts with a busy entry); N slices done; the baseline on a base branch is red; a working tree is dirty; the store does not answer; the same environment failure (permissions, missing toolchain) on two tasks in a row. The repositories end as described in "End of the run".

## Forbidden (also written into the agents)

`git push`, `git fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; editing a base branch; editing the zones that the rules file lists (vendored code); editing outside the entry and its WPs; committing a plan, a review or an entry text (they live in the store; the working copies stay in `~/.claude/plans/`); a write to the store other than the conductor's `claim`, `review`, `stop`, `close` and `plan put` and a developer's `todo edit` of its own entry's body; `todo admin ...`; moving the pointer of a submodule in the game repository; putting Homebrew LLVM on `PATH` for `cmake --preset`. The exceptions are the `close` mode, started by the owner after a merge, and the `todo-merge` skill (merge into the base branch, `branch -d` of the merged branch, the pointer commit, only after the owner's yes), which runs the same closing steps. The agents keep the whole list, always.

## Report

For the owner, in Russian, short. First line: **on which branch of which repository the checkout stands now** (`todo/<branch-id>` in `<R>`; in a worker also the folder, `cd <absolute path of the worker>`), how to check it live (build with the `build` skill: after the switch a rebuild is expected; how to start the game; the manual items the architect left in the plan), and the other branches of this run (`git -C <R> switch todo/<other>`). Then a table "entry, area, branch, commits, build/tests, reviewer verdict, plan and reviews" (the last column holds links in the same format as in the chat: the plan and each review file, and the pages in the store); the skipped entries with reasons (`conflicts with <id>` among them); what was not checked (platforms, manual items that the architect left for the owner, the checklist when it was unavailable); a mirror error or a failing outbox from `todo ping`. End with the owner's queue: `todo list --status review --area <area>` and, per area of the rules file, `git -C <R> branch --list 'todo/*' --no-merged <base>`, and the offer: run `/todo-merge` (report, ask, merge, close, delete the branch; it switches to the base branch itself), or merge by hand and run `/todo-loop close`, which closes the entry in the store (for a slice, sets it back to `open`). For `close` itself the report names the closed and reopened entries and the state of the mirror.
