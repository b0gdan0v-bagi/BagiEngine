---
name: todo-loop
description: Works through the entries of the todo store one by one, each in its own local branch todo/<id> cut from the base branch of its repository, with a classifier, an architect, developers and a reviewer as separate agents, and leaves the last finished branch checked out in status review so the owner can try it live. With `close` it finishes merged branches (closes the entry in the store). Use when the user asks to run the todo loop, work through the todo list on its own, take the next todo entry, list what the loop could take, or says a todo branch is merged and its entry should be closed.
---

# todo-loop

You are the **conductor**. You do not read or write code: you run git, move the status of one entry in the todo store, keep its plan and reviews in the store, start agents (`.claude/agents/todo-*.md`), and keep short reports in your context. All work on code happens in the agents. The store and its CLI are described in `<engine>/tools/todo/CLAUDE.md`.

State lives in the **todo store** (the status of an entry: `in progress` with its branch means taken; its slice plan and reviews) and in git (the branch `todo/<id>*` with the work). A branch lives only in the checkout that made it (branches are never pushed), so the loop is stopped and started again in that checkout, and the review and the merge of the entry happen there; the store records where (`base`, `origin`). **Nothing about the state of an entry is committed**: no status commits, no entry files, no plans.

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
- **Busy files.** Two branches that change the same file conflict at the merge, and `todo-merge` does not resolve conflicts, so a loop does not take an entry that would change what another taken entry changes (steps 1 and 2). The busy files of a taken entry (`in progress` or `review`, with a branch) are what its branch changed, `git -C <R> diff --name-only <base>...<branch>`, plus the `## Files` sections of its plans in the store: the first slice plan `todo-loop-<branch-id>.md`, the newest one (`planShown`) and, for a through entry, the stage plan (`stagesPlan`). Entries of different areas never meet (different repositories). Two paths meet when they are equal or one is a folder (ending with `/`) that holds the other. The shared files of the rules file (section "Workers") do not count alone. A stacked entry and its blocker never conflict with each other (section "Stacked branches"): the stacked branch holds the blocker's commits, so what both change is merged in order, not twice.
- In a worker, never run `git submodule update` in the game repository: the worker's `engine/` is a worktree of the main checkout's engine module, `worker.py` turned the update off for the worker, and `worker.py check` says whether the shared config is still right.
- **Reports to the launcher.** With `report-to=<session>` the loop sends its news to that session with `SendMessage` (`to`: the name as given), so the owner reads every worker in one place. A message is plain text, in Russian, its first line `w<k> · <id>: <what happened>` (`w<k>` is the worker folder's name, or `main` outside a worker), at most about 15 lines, with the absolute links of the chat (plan, reviews, pages in the store). Send one when: a slice reaches `review` (step 6: branch, repository, commits, build/tests, reviewer verdict, the note), a task stops ("Stopping a task": the reason), the loop stops with nothing taken or on an error ("Stopping the whole loop"), a permission or a question waits for the owner (the loop asks none of its own; the message says the worker waits and how to answer, `python3 engine/tools/todo/worker.py attach <k>` from the main checkout, which opens the session by its id), and at the end of the run (the report of section "Report", shortened to the first line, the table and the owner's queue). A send that fails (the session is gone, it refused) is a note in the own report; the loop does not stop or retry for it. Never send anything else: no progress messages per run, no questions to the launcher, no instructions.
- Start the sessions a minute or so apart: two loops that choose at the same moment can still take two entries that meet (the window is the time between the check of step 2 and the stub plan).

## Stacked branches

A dependency in the store is a hard wait until the blocker is `closed` (`<engine>/tools/todo/CLAUDE.md`, "Dependency"), and a blocker is closed only after the owner merges it. So that a chain (A blocks B blocks C) does not stop at every link, the loop may take an entry whose blocker is done but not yet merged, and cut its branch from the blocker's branch instead of the base: a **stacked** entry on its **parent** branch.

- **When.** The entry is `open` and `blocked`, and exactly one of its `blockedBy` is not `closed`: the parent entry, in status `review`, of the **same area** (one repository: a game entry never stacks on an engine branch), whose `branch` exists in this `<R>` and is not merged into `<base>`, and whose `statusNote` does not have `slice K of M, after the merge set the status back to open` (a slice that leaves its entry open does not finish what the stacked entry waits for). The parent is not itself stacked on an unmerged branch (one level only: a chain is worked link by link as the merges come). Anything else stays a plain `blocked by <ids>`.
- **The branch.** `git -C <R> switch -c todo/<branch-id> <parent branch>` (it works while the parent is checked out in another worker: a new branch only reads the ref). The claim still names `--base <base>`: the entry is merged into the base, after its parent. The stub plan and the slice plan start with the line `Stacked on todo/<parent branch-id> (entry <parent id>): merge that first.`
- **The range.** The final review of this branch reads `<parent branch>...HEAD`, not `<base>...HEAD`: the parent's commits were reviewed under the parent.
- **The parent changes later** (the owner asked for more on it, a fix): the stacked branch is never rebased (a rebase rewrites commits the owner may be trying, and `reset` and `--force` are forbidden). It keeps the parent's commits it was cut from; the rest reaches it when the base, with the merged parent, is merged in by `todo-merge` (which checks the merge with `merge-tree` and refuses a conflict).
- **The note.** `todo review` of a stacked slice ends its note with `; stacked on todo/<parent branch-id>`, so the owner and `todo-merge` see the order.

## Paired: an engine entry and the game entry that waits for it

In a game (the engine area is a row of its rules file) work often comes as a pair: an engine entry E and a game entry G that is blocked by it (`todo dep add G E`). The owner can try G only on top of E and checks both later anyway, so the worker that finished E takes G at once, without waiting for E's merge:

- **When.** G is `open` and its only open blocker is E: an engine entry in `review` whose branch exists in `engine/`, is not merged into the engine's base, and whose `statusNote` has no `slice K of M, after the merge set the status back to open` (E is finished, not a slice). Step 1 counts such a G as a candidate `paired on <E>`, after the unblocked candidates of its area.
- **Right after E.** When this run puts E into `review` (step 6), its paired game entries come next. In a worker `w<k>`: `python3 <engine>/tools/todo/worker.py queue <k> --add <G> --first` (several: in `id` order) and, as the last command, the detached start of section "Through" (a fresh conductor). Outside a worker the next slice of this run takes G (when `N` allows).
- **The branches.** G's branch is cut from the game's base as usual, then `git -C engine switch --detach todo/<E branch-id>` puts `engine/` on E's tip (it works while E's branch is checked out in another worktree). The stub plan and the slice plan start with `Paired on engine todo/<E branch-id> (entry <E>): merge that first, then the pointer.` Nobody commits the `engine` pointer in G's branch (`todo-merge` does after E's merge); the game shows `engine` changed, which the preconditions allow.
- **The review** reads the game's `<base>...HEAD` with `engine/` at E's tip (E's own changes were reviewed under E); a test that fails here and not in the baseline is G's to explain, even when E's code is where it fails.
- **The note.** `todo review` of G ends its note with `; paired on engine todo/<E branch-id>`. `todo-merge` and `worker.py merge` refuse an entry with an open blocker, so E is merged first (with its pointer commit), then G.
- **E changes later** (a remarks round on E): G's branch is not rebuilt by itself; when E comes back to review, the owner sends G back with remarks if it needs to follow.

## Through: slices that do not wait for the merge

An entry with `slicing: through` in the store (the owner sets it: `todo edit <id> --slicing through`, or the panel's toggle; it is read at every slice, since each slice may be a new session that knows only the id) keeps the slice as the unit of review, not of merge: all its slices go on one branch, each is reviewed on its own range, and the owner merges once. Every other rule of the loop stays.

- **Stage plan.** The architect of the first slice also writes the stage plan of the whole entry, `todo-loop-<branch-id>-stages.md` (`todo-architect.md`, "Through"); store it with `todo plan put ... --kind stages`. Each stage is one slice and says where it ends: `size` (more stages follow, none needs the owner), `gate` (the next one needs the owner) or `end`.
- **Names.** The **plan id** of slice 1 is `<branch-id>`, of slice K ≥ 2 `<branch-id>-s<K>`; its plan is `todo-loop-<plan id>.md` and its reviews `todo-loop-<plan id>-review-<name>.md`. Wherever this skill names `todo-loop-<branch-id>` for a slice's plan or reviews, a through slice uses its plan id.
- **Slice start.** Before the first developer of slice K ≥ 2, write the line `Slice start: <git -C <R> rev-parse HEAD>` under the plan's title; its review reads `<slice start>...HEAD` (slice 1: the usual range).
- **After `APPROVE`**, by the stage's end: `end`: step 6 as usual, the note saying `through, slices 1-M, each reviewed`, and the owner merges once. `gate`: step 6 with the usual `, slice K of M, after the merge set the status back to open` (after the merge the next slice is cut from the base like any other). `size`: **no `todo review`**; `todo note <id> --note "through: slice K of M approved at <short sha>; next: slice K+1"`, the status stays `in progress`, and the stage plan gets the stage's box ticked (`plan put`).
- **Cost cap.** Before a `size` slice hands over: if the entry has a `costCap`, `python3 <engine>/tools/todo/worker.py cost <id> --json` while the branch is still checked out (it counts the sessions of the workers that hold the entry's branch); `over` true means `todo stop <id> --reason "cost cap $<cap> reached after slice K of M ($<dollars>)"` and no next slice. The cap is checked only between slices.
- **A fresh conductor per slice.** This session must not carry five slices in its context, so a `size` slice ends this run (it counts as the last of `N`). In a worker `w<k>`: `python3 <engine>/tools/todo/worker.py queue <k> --add <id> --first`, the report, and as the very last command `python3 <engine>/tools/todo/worker.py start <k> --detach --wait-free 1800 --queued-only --delay 0` (plus `--report-to <session>` when this run has one): it waits for this session to end and starts `/todo-loop 1 <id>` in the same worker. Outside a worker the next slice goes on in this session (and counts toward `N`).
- **Continuing.** `<id>` of a through entry `in progress` whose `statusNote` starts with `through: slice K of M approved`: no classifier and no claim; switch to `todo/<branch-id>`, start the architect for slice K+1 with the stage plan, then steps 4 to 6.

## Remarks: a review branch sent back

The owner sends an entry in `review` back (`worker.py remarks <id> --file <F>`, the panel's **Return with remarks**): the remarks are the plan `todo-loop-<branch-id>-remarks-<n>.md` (kind `remarks`), the entry is `in progress` again with the note `returned with remarks, round <n> (<plan>); was: <the review note>`, and it waits first in the queue of the worker that holds its branch. A loop with `<id>` of such an entry:

1. No classifier, no claim, no architect. Switch to `todo/<branch-id>`, read the remarks (`todo plan get <plan> --stdout`), and note `Remarks start: <git -C <R> rev-parse HEAD>`.
2. One developer (sonnet, high) gets the remarks as its findings (commits `Remarks fixes: <what>`), the newest slice plan and the remarks file.
3. Step 5 on `<remarks start>...HEAD`, with the remarks in place of the WPs' goals (is each remark done, and nothing else broken); reviews named `todo-loop-<branch-id>-review-remarks-<n>.md`.
4. Step 6: `todo review` with the note `remarks round <n> fixed: <one line>`, followed by the slice suffix the `was:` part had (`, slice K of M, after the merge ...`, `; stacked on ...`).

## Arguments (all optional)

`/todo-loop [N] [<id>] [list] [plan-only] [close] [models=role:model,...] [report-to=<session>]`

| Argument | Effect |
|----------|--------|
| `N` | Take at most N slices. Default 1. |
| `<id>` | Work on this entry. If it is `in progress` on an unmerged branch `todo/<id>*`, continue that branch: a remarks round (section "Remarks") when its note starts with `returned with remarks`, the next through slice ("Through") when it starts with `through: slice`, else from the first WP whose boxes are not ticked. Ids are unique over every area. |
| `list` | Only the classifier runs. No branches, no writes to the store. |
| `plan-only` | Classifier and architect. The entry is claimed, the branch is cut (no commit), the plan is in the store. |
| `close` | After the owner has merged branches: close their entries in the store (section "Closing merged entries"). Nothing else runs. |
| `report-to=<session>` | Also send the reports to this Claude session (section "Reports to the launcher"). `worker.py start --report-to` passes it, so the session that started the workers hears from all of them. |
| `models=classifier:haiku,reviewer:sonnet` | Override the model of a role for this run, over the levels below. `haiku`, `sonnet` and `opus` are allowed; `opus` is used only by the final review, the other roles stay at or below `sonnet`. |

## Roles and models

| Role | Agent | Model, effort | Does |
|------|-------|---------------|------|
| Classifier | `todo-classifier` | haiku, medium | Chooses the next eligible entry, answers JSON (with a `size` and a `level`). |
| Architect | `todo-architect` | sonnet, high | Cuts the slice into work packages (WP) with a level each and groups them into runs, writes `~/.claude/plans/todo-loop-<branch-id>.md`, may answer `BLOCKED`. |
| Developer | `todo-developer` | by the level | One **run** (one or more consecutive WPs): code, tests, docs, format, one commit `WP k/M: ...` per WP. A new agent for every run. |
| Reviewer | `todo-reviewer` | by the slice | The final review of the slice: fresh context, reads the diff, builds and runs the tests, answers `APPROVE` or `CHANGES`. |

**Runs and levels.** Most of the cost is the context an agent reads (the docs and code of "Read first", diffs, build and test output). A fresh developer per WP reads the same files again for every WP of an area, and a review per WP rarely finds what the final review of the slice would not. So a developer does a **run** of WPs in one context, and the slice has **one review**, at the end, on the strong model.

The architect gives every WP a level (`light`, `normal`, `deep`; `todo-architect.md`, "Level of a WP") and a run number (`**Run:** r`, "Runs"); WPs with the same run are consecutive. A WP without a run is a run of its own; a WP without a level is `normal`. For size S the classifier's `level` is the level of the one WP. The level of a run is the highest level of its WPs. Start the developer with these **`model` and `effort` of the Agent tool, always given explicitly**:

| Level of the run | Developer |
|------------------|-----------|
| `light` | sonnet, medium |
| `normal` | sonnet, high |
| `deep` | sonnet, high |

The **final review** runs on **opus, high**; on sonnet, high when every WP of the slice is `light`. Its second round (`final-r2`) runs on the same. A developer that fixes review findings runs on sonnet, high. `models=` overrides the model of a role for the whole run of the loop; the effort stays by the level. The conductor itself (this session) needs neither code nor deep thought: `worker.py start` runs it on sonnet, medium.

Start agents with the Agent tool and `subagent_type` set to the agent's name, one at a time, in the foreground (the checkout is shared, and a background agent cannot ask for permissions). Never use SendMessage to a developer that has finished: each run gets a new one. Every agent gets the area of the entry (`<area>`, `<R>`, `<base>`, `<engine>`) in its task. Size **S** (from the classifier): skip the architect and give the developer the entry as one WP; the plan file is only the stub of step 2 (its `## Files`).

## Plans and reviews: in the store, worked on as local files

Agents read and write files, so the plan of a slice and each review have a working copy in `~/.claude/plans/` (outside every repository) and their truth in the store:

- after the architect: `todo plan put todo-loop-<branch-id>.md --entry <id> --kind slice`;
- after every developer (it ticks boxes in the local file) and after you append review notes: `todo plan put todo-loop-<branch-id>.md` again;
- every reviewer answer: save it verbatim to `~/.claude/plans/todo-loop-<branch-id>-review-<name>.md` (`<name>`: `final`, `final-r2`) and `todo plan put <that name> --entry <id> --kind review`;
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

1. **Choose.** Go to the base for every area. First close the entries whose branches the owner has merged ("Closing merged entries", steps 2 and 3): a merged branch is the owner's yes, so this needs no question; name them in the report. Their dependents may be unblocked now. Candidates: `todo list --status open --area <area> --json` for each area of the rules file, in the order of its rows (never all areas: "Only the areas of the rules file"), ordered within an area: no stage first, then stages 1 to 6, then by `id`. An entry with `blocked: true` (a blocker in its `blockedBy` is not closed: `tools/todo/CLAUDE.md`, "Dependency") is not a candidate, unless it can be **stacked** (section "Stacked branches") or **paired** (section "Paired"): then it is a candidate with its parent (`stacked on <parent id>`, `paired on <E>`), after the unblocked candidates of its area. Name the other blocked entries in the report as `blocked by <ids>`. Entries `in progress` are taken (by the loop, with a branch, or by the owner); the loop never takes them. As a consistency check, an unmerged branch `git -C <R> branch --list 'todo/*' --no-merged <base>` whose entry is `open` in the store is a note in the report (the owner cut it by hand); skip that entry. Give the classifier the candidates (`area`, `id`, `title`, `stage`, `summary`, and `stackedOn` / `pairedOn` for a stacked or paired one) and the **busy entries**: `todo list --status "in progress,review" --area <area> --json` for each area of the rules file, each with its busy files ("Parallel workers"). Take the first `eligible` entry in its order whose `touches` meet no busy file of its area (you compare the lists; the classifier's `next` does not know); name the others in the report as `conflicts with <id>: <paths>`. Check the chosen one yourself: it is in the list and `open`. With `<id>`, skip the classifier unless `list`, and take the entry even when it meets busy files (say so in the report). No eligible entry, or every eligible one conflicts: stop the loop.
2. **Claim and branch.** Right before the claim, list the busy entries again: an entry taken since step 1 (by another loop) whose busy files meet the chosen entry's `touches` means: take the next eligible one. `todo claim <id> --branch todo/<branch-id> --base <base>` (`<base>` of the entry's area, written as the area table has it, e.g. `games/chess`; the CLI fills `origin` as `<game repository>@<host>`, so run it from the root of the repository the loop was started in (a game: not from inside `engine/`); `<branch-id>` is `<id>`, or `<id>-2`, `-3` for the next slice of an entry whose earlier branch is merged: the first name with no branch in `<R>`). A refusal (HTTP 409: someone else took it) means: take the next candidate. **At once** store the **stub plan**, so other loops see what this entry will change: write `~/.claude/plans/todo-loop-<branch-id>.md` with the line `# <title>: files` and a `## Files` section (the classifier's `touches`, one `- <path>` per line) and `todo plan put todo-loop-<branch-id>.md --entry <id> --kind slice`; the architect replaces it with the plan and keeps the section. Then `git -C <R> switch -c todo/<branch-id> <base>`, or for a stacked entry `git -C <R> switch -c todo/<branch-id> <parent branch>` (its stub plan starts with the `Stacked on` line); a paired entry also puts `engine/` on its engine entry's tip ("Paired"). Before cutting a stacked branch check the parent again: still `review`, branch not merged; otherwise take it as a plain entry (parent merged and closed) or skip it (parent stopped or reopened). **No commit**: the status is in the store.
3. **Plan.** Start the architect (not for size S) with the entry id, area, branch, size, the parent branch for a stacked entry, and the `plan`, `planShown`, `planProgress` of `todo show <id> --json`; for a through entry also `through`, the slice number K and the stage plan (`stagesPlan`; none for slice 1). `BLOCKED` goes to "Stopping a task" (no plan, no link). Otherwise store the plan (`todo plan put ... --kind slice`, above) and, before step 4 starts, write one line to the owner in the chat with links to the plan: `[Plan: <title>, slice K of M](file:///<absolute path>)` (the path absolute, the home directory expanded, never `~`; the architect answers with such a path) and the page in the store (`todo plan url todo-loop-<branch-id>.md`), plus the number of WPs and of runs. Size S keeps the stub plan and gets no link. The entry's `plan` (the owner's plan) is not changed; the slice plan is a separate plan of the entry. With `plan-only`, end the task here and go to step 7.
4. **Implement, run by run.** For each run a new developer (model and effort by the run's level, "Roles and models"), with the numbers and texts of its WPs, the plan path, the entry id, the area and the branch. It commits each WP on its own, green. If it ends with `WP incomplete: ...`, start a new developer for the rest of the run (at most 2 times; then the architect cuts again or the task stops). After each developer: `todo plan put todo-loop-<branch-id>.md` (its ticked boxes). No review between runs: the next run starts at once.
5. **Review the slice, once.** After the last run, one reviewer (opus, high, or sonnet, high for an all-`light` slice; "Roles and models") on `<base>...HEAD` (`<parent branch>...HEAD` for a stacked entry, `<slice start>...HEAD` for a through slice K ≥ 2), with the plan path, the area, the baseline file and the checklist flag: the diff against every WP's goal and acceptance, full build and `ctest`, is the plan closed, are the docs and the entry consistent. `CHANGES` with blocking findings: a new developer (sonnet, high) gets the findings and commits the fixes (`Review fixes: ...`), then a second round (`final-r2`) on the same model gets the first answer and checks that each blocking finding is fixed, reads the fix commits, and builds and tests again; at most 2 rounds. Non-blocking findings go to "Review notes" in the plan file (then `plan put`). **Every reviewer answer is stored and gets a link in the chat:** the reviewer cannot write files, so save its answer verbatim to `~/.claude/plans/todo-loop-<branch-id>-review-<name>.md` (`<name>`: `final`, `final-r2`), `todo plan put <that name> --entry <id> --kind review`, and write one line `[Review: APPROVE|CHANGES, <n> blocking](file:///<absolute path>)`.
6. **Finish.** A through slice ends as section "Through" says (a `size` stage: no review, `todo note` and the next slice). Otherwise `todo review <id> --branch todo/<branch-id> --note "<one line: what was done, how it was checked>"`. If the slice does not close the entry, end the note with `, slice K of M, after the merge set the status back to open`; a stacked slice ends it with `; stacked on todo/<parent branch-id>` (after the slice part, when both apply). **No commit.** If a WP changed what the entry says (the developer did it with `todo edit`), check it with `todo show <id>`. Remember the branch and `<R>` as the **last review branch** of the run.
7. **Next.** Go to the base and go to step 1 until a stop condition. The work branch stays checked out only at the end (see "End of the run").

You write to the store (`claim`, `review`, `stop`, `close`, `note`, `plan put`) and to the plan store, and in a worker to its queue and `worker.py start` ("Through"); you make no edit to a file in a repository. Everything else is the agents'.

## End of the run

The owner wants to try a change live before merging, so the run does not end on the base branch:

- If at least one slice of this run ended in status `review`, switch the repository of the **last review branch** to it: `git -C <R> switch todo/<branch-id>`, and stay. Every other area repository goes back to its starting branch. For an engine slice in a game, only `engine/` moves; the game repository stays where it started and builds with the engine on the work branch (it shows `engine` as a changed pointer, which the precondition above allows). For a paired game entry the game stays on its branch and `engine/` on the engine entry's tip, so the owner tries both at once.
- Otherwise (`list`, nothing taken, every task stopped, `close`) every repository goes back to its starting branch.
- A branch whose entry stopped (`in progress ... stopped`) is never the one to stay on.
- The tree must be clean for the switch (the developers commit their work). Never `stash` or `reset` to get there; if the switch fails, say so in the report and leave the repository at the base.

## Closing merged entries (`close`)

The owner starts it after merging by hand, and step 1 of the loop does steps 2 and 3 of it by itself. No agents: you do it yourself. It works area by area and changes nothing in git.

1. Preconditions: the repositories are clean (as above), `todo ping` answers. Go to the base.
2. Find the entries: `todo list --status review --area <area> --json` for each area of the rules file; for each entry of the area, its `branch` must be in `git -C <R> branch --list 'todo/*' --merged <base>`. An entry in review whose branch is not merged waits for the owner and is only named; one whose branch does not exist in `<R>` belongs to another checkout and is left out. An unmerged branch whose entry is in another status (for example `in progress ... stopped`) is only named.
3. Per entry: if its `statusNote` has `slice K of M, after the merge set the status back to open`, `todo close <id> --reopen` (open again, branch cleared, the body stays). Otherwise `todo close <id>` (closed; the entry stays in the store with its history, and its Vikunja task is marked done by the mirror).
4. Report the closed and reopened entries. There is no tracker step: the service mirrors the change into Vikunja by itself (`todo ping` shows the outbox; a failing one is a note).

Never in `close`: git `push`, `fetch`, `branch -d` or `-D`, a commit, or any entry other than the ones found in step 2.

## Stopping a task (status stays `in progress`)

Commit what exists (the developer's last green state), `todo stop <id> --reason "<reason>"`, go to the base, take the next entry. Reasons: `BLOCKED` from the architect; the same failure after 3 attempts; 2 review rounds without `APPROVE`; a WP not closed after 2 restarts of the developer; the work grew beyond the entry.

## Stopping the whole loop

No eligible entry (or every eligible one conflicts with a busy entry); N slices done; a through `size` slice or an engine entry with paired game entries that handed the next work to a fresh session; the baseline on a base branch is red; a working tree is dirty; the store does not answer; the same environment failure (permissions, missing toolchain) on two tasks in a row. The repositories end as described in "End of the run".

## Forbidden (also written into the agents)

`git push`, `git fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; editing a base branch; editing the zones that the rules file lists (vendored code); editing outside the entry and its WPs; committing a plan, a review or an entry text (they live in the store; the working copies stay in `~/.claude/plans/`); a write to the store other than the conductor's `claim`, `review`, `stop`, `close`, `note` and `plan put` and a developer's `todo edit` of its own entry's body; `todo admin ...`; moving the pointer of a submodule in the game repository; putting Homebrew LLVM on `PATH` for `cmake --preset`. The exceptions are the `close` mode, started by the owner after a merge, and the `todo-merge` skill (merge into the base branch, `branch -d` of the merged branch, the pointer commit, only after the owner's yes), which runs the same closing steps. The agents keep the whole list, always.

## Report

For the owner, in Russian, short. First line: **on which branch of which repository the checkout stands now** (`todo/<branch-id>` in `<R>`; in a worker also the folder, `cd <absolute path of the worker>`), how to check it live (build with the `build` skill: after the switch a rebuild is expected; how to start the game; the manual items the architect left in the plan), and the other branches of this run (`git -C <R> switch todo/<other>`). Then a table "entry, area, branch (with `stacked on todo/<parent>` for a stacked one: merge the parent first), commits, build/tests, reviewer verdict, plan and reviews" (the last column holds links in the same format as in the chat: the plan and each review file, and the pages in the store); the skipped entries with reasons (`conflicts with <id>` among them); what was not checked (platforms, manual items that the architect left for the owner, the checklist when it was unavailable); a mirror error or a failing outbox from `todo ping`. End with the owner's queue: `todo list --status review --area <area>` and, per area of the rules file, `git -C <R> branch --list 'todo/*' --no-merged <base>`, and the offer: run `/todo-merge` (report, ask, merge, close, delete the branch; it switches to the base branch itself), or merge by hand and run `/todo-loop close`, which closes the entry in the store (for a slice, sets it back to `open`). For `close` itself the report names the closed and reopened entries and the state of the mirror.
