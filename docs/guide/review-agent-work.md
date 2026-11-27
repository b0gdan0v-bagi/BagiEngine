# What to review in an agent's work

You will take an entry that the loop left in `review` and decide what to do with it: read what the agents wrote down in the order that finds problems fastest, try the branch live, know what to be suspicious of, and then merge it, send it back, or leave it. The agents' own rules are the source of truth ([`.claude/agents/todo-reviewer.md`](../../.claude/agents/todo-reviewer.md) for the reviewer, [`.claude/skills/todo-merge/SKILL.md`](../../.claude/skills/todo-merge/SKILL.md) for the merge); this page tells the same story for a person.

You need an entry in `review` ([The todo loop](todo-loop.md)) and a checkout where you can build ([Run the demo](run-the-demo.md)). The reviewer already read the whole branch once and ran the build and the tests. You read it again because it is your engine and your merge.

## Where the work waits

The entry is in `review`, and the note of its status says what was done and how it was checked. The branch `todo/<id>` is checked out in the worker folder that did the work, or, when no worker folder holds it (`git worktree list` shows), you switch to it in your main checkout; every branch is visible from every folder, because they share one `.git`.

<!-- command -->
```bash
python3 tools/todo/todo.py show <id>
python3 tools/todo/todo.py plan list --entry <id>
git worktree list
git log --oneline main..todo/<id>
git diff --stat main...todo/<id>
```

`show` prints the entry and its status note, and `plan list` names its plans and reviews. `git worktree list` shows which folder holds which branch, and the last two commands show the commits and the files the branch changed against `main`. All of them only read.

To try the branch live, build and run it as in [Run the demo](run-the-demo.md), in the folder that holds it. A worker folder has its own build folder, so a rebuild there does not touch yours. If you switch your main checkout to the branch instead, the build there is of the branch until you switch back.

## The order to read in

1. **The status note**: what the loop says it did and how it checked.
2. **The plan**: for each work package its goal and the command that proves it. Read the goals first, and only then the diff, so you know what the change was meant to be.
3. **The review report**: below.
4. **The decisions list**: below.
5. **The diff**: `git diff main...todo/<id>`, one commit per work package, `WP k/M: <what>`.
6. **The program**: build it and look at what the entry asked for.

Plans and reviews are read with `plan get <name> --stdout`, as on the page about the loop. They are never copied into the repository.

## The review report

The reviewer ends its answer with this block:

<!-- source: .claude/agents/todo-reviewer.md -->
```text
VERDICT: APPROVE | CHANGES
Build: <command> -> ok | failed (first error, file:line) | none (nothing is built)
Tests: <command> -> <n passed, names of failing tests, compared with the baseline>
Findings:
- [blocking:<kind>] file:line — what is wrong and what would be right; verify: <check> [checklist]
- [followup|note] file:line — what is wrong and what would be right [checklist]
Checklist: used | checklist not installed on this machine | no checklist in this repository
Decisions: <the lines of the plan's section, one per line, or "none">
```

Line by line:

- **`VERDICT`**: `APPROVE` or `CHANGES`. You see a report only after the loop finished, so a `CHANGES` in it is never the last word: a blocking finding was fixed by a new developer and checked again, and the entry reaches `review` only after an `APPROVE`. Two reviews without one park the entry instead ([The todo loop](todo-loop.md), "When it stops").
- **`Build`** and **`Tests`**: the commands the reviewer ran and what they gave, with the failing tests compared against a baseline taken before the change. A test that was already red before the branch is not this branch's fault; a test that was green and is red now blocks.
- **`Findings`**: `blocking` ones were fixed before you see the report. A `followup` is a small defect that did not block: after the merge `/todo-merge` files it into the area's rolling entry of review follow-ups, so you do not have to remember it. A `note` is an opinion or a question; nobody files it, so read the notes yourself.
- **`Checklist`**: whether the reviewer also walked the diff through the code checklist installed on its machine.
- **`Decisions`**: the plan's list of what the agents decided on their own, copied into the report.

## The decisions list

An agent that meets a small question the entry left open does not stop to ask. It decides, and writes a line into the plan. The plan template has this section:

<!-- source: .claude/agents/todo-architect.md -->
```text
## Decisions made by the worker
(only when you decided something: `<decision> - why - how to undo`, one per line)
```

A good line is small and reversible, and its last part tells you how to undo it: a name, a place for a file, a value the entry left open. The reviewer checks that a visible choice in the diff is on the list.

Some decisions are not for an agent at all: the merge into the base, `git push`, a licence, a product decision, a change of a public format or protocol that the entry did not ask for, and anything against the entry's text. These are yours (the loop's text calls them the owner's list). If one of them is in the list anyway, the branch overstepped: send it back.

The plan's section `Left for the owner` lists what the agents could not check: a look on another system or a device, or a picture. The reference pictures of the scenario tests are the usual example, because a tolerance chosen on one machine may be wrong on another ([Test a game](test-a-game.md)). Do those checks before you merge.

## Red flags

The reviewer looks for most of these too. You look because it is your branch.

- **A test changed or loosened so the change passes.** The test was the promise; a change that edits it has no promise left.
- **A reference picture retaken with `--update`, or a tolerance raised, with no reason given.** The picture is what the scenario compares against, so retaking it hides a change instead of checking it ([The control API from Python](control-api.md), [Test a game](test-a-game.md)).
- **A content file of a game edited by hand.** A change of a content format goes through a migration step, and the owner of the game runs the tool; a hand edit skips the step.
- **A plan, a review or an entry text inside a commit.** They live in the store, never in git.
- **A vendored folder edited.** Vendored code is never changed here.
- **A public format, protocol or function changed that the entry did not ask for.** Games and tools depend on them.
- **Files in the diff that the plan's `## Files` does not name.** Either the plan was wrong or the change wandered.
- **A claim that something was checked, with no command behind it.** A line in a doc or a note must name what was run.
- **A diff much larger than the entry.** A small request that became a rewrite is a sign that it grew past what you asked.
- **A decision on the list that was yours.** See above.

## Then

You can merge, send it back, or leave it.

**Merge.** `/todo-merge` shows a report (the entry, the branch, what was done and how it was checked, the commits and the files, the review's notes and what is left for you), then asks whether to merge: merge, close and delete the branch; merge only; or no. Merge only merges and leaves the entry and the branch as they are. On the full answer it asks which version bump (when the repository keeps a changelog), merges with a merge commit, closes the entry (or sets it back to `open` when the slice did not finish it), files the review's follow-ups, writes the changelog fragment and a version when the repository asks for them, and deletes the branch; it may then ask whether to run the fast perf set. It never pushes.

<!-- illustration: the skill todo-merge; shown, not run on this page, because it merges into the base and writes to the store -->
```text
/todo-merge <id>
```

**Send it back.** Write what is wrong in a file and give it to the branch's worker: `worker.py remarks`, as on the page about the loop. The entry goes back to `in progress`, a developer does exactly those remarks, a check follows, and it returns to `review`.

**Leave it.** The entry stays in `review` and the branch stays; nothing else happens until you act. Pushing the base afterwards is yours: no agent ever does it.

## What you did

You learned where a branch in `review` waits and how to try it, the order to read what the loop wrote, what each line of the review report means, what a decisions list should and should not hold, which signs in a diff to be suspicious of, and how a merge, a send-back and a wait differ.

Next (planned): the C++ rules of the engine explained, and where the data lives
