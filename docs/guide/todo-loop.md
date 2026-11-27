# The todo loop

You will see what the todo loop does with an entry you filed: who works on it, what each of them leaves behind, what you can see at each step, and the few things that are only yours. The agents' own rules are the source of truth and live in `.claude/skills/todo-loop/SKILL.md`; this page tells the same story for a person, and a page that the skill contradicts is a bug of the page.

You need an entry in the store ([Ask for a change](ask-for-a-change.md)) and the owner's access to it.

## The idea

You file an entry. A loop takes it, works on it in a branch of its own, `todo/<id>`, and a fresh reviewer checks the whole result once. Then the entry waits for you in the status `review`. Nothing is pushed, and nothing reaches the base branch (`main`) until you say so. One entry is one branch, one review and one merge.

## The roles

The loop is a session you start, the **conductor**. It writes no code: it runs git and the store and starts the others one at a time, each with a fresh context.

| Role | What it does | What it leaves behind |
|------|--------------|-----------------------|
| Conductor | claims the entry, makes the branch, starts the others, moves the status | the status and its note in the store |
| Classifier | picks the next entry that can be done without you, and skips the rest with a reason | a verdict, one line per entry |
| Architect | checks the entry against the code, then cuts the work into work packages (WP), each with its acceptance | a **plan** in the store |
| Developer | does one run of consecutive WPs, tests green, one commit per WP | commits `WP k/M: <what>` on the branch |
| Reviewer | reads the whole branch once, builds, runs the tests, answers `APPROVE` or `CHANGES` | a **review** in the store |

Nobody reviews a work package on its own: the slice gets one review at the end. The reviewer cannot edit files: the reviewer's findings go back to a new developer, who commits `Review fixes: <what>`. The loop's text gives the roles and their models in its section "Roles and models"; the models change, so this page names none.

## The life of an entry

| Status | What it means | What you see |
|--------|---------------|--------------|
| `open` | filed, nobody has taken it | the entry in the store |
| `in progress` | a loop took it; the branch `todo/<id>` exists | the branch, and the status note (`parked: ...` or `stopped: ...` after a stop) |
| `review` | the reviewer approved; the branch waits for you | the note says what was done and how it was checked |
| `closed` | you merged the branch and the entry was closed | the history of the entry |
| `deferred` | set aside; the loop takes only `open` entries, so it waits | the entry, with that status |

<!-- command -->
```bash
python3 tools/todo/todo.py list --status review
git branch --list 'todo/*'
```

The first command lists the entries waiting for you, one line each. The second lists the branches in this repository; a branch lives only in this clone and its worker folders, because branches are never pushed.

## A big entry is cut

An entry that is too big for one review is cut into **slices**, and the architect writes the whole cut once as a stage plan. A `through` entry (the default) keeps all its slices on one branch: each slice is reviewed on its own range, and you merge once. A stage ends in one of three ways: `size` (more stages follow and none needs you, so the entry stays `in progress` and the next slice starts), `gate` (the next stage needs you: the entry goes to `review`, and after the merge it is `open` again, not `closed`) or `end`. `/todo-merge` warns you when a merge does not finish the entry.

Plans and reviews are read from the store, never written into the repository:

<!-- command -->
```bash
python3 tools/todo/todo.py plan list --entry <id>
python3 tools/todo/todo.py plan get <name> --stdout
```

`plan list` names the plans and reviews of an entry; `plan get <name> --stdout` prints one (without `--stdout` it writes a file, and it refuses a folder inside a git work tree, because a plan is never committed).

## Workers

One session can run one loop. To work on several entries at once you make **worker folders**: git worktrees of the same repository, in `../<repo>.workers/w1`, `w2` and so on, each with its own build folder and its own loop. They share one `.git`, so every branch is visible in every folder and in your main checkout.

<!-- source: tools/todo/CLAUDE.md -->
```bash
python3 tools/todo/worker.py create 3      # <parent>/<name>.workers/w1..w3; --base, --preset, --no-configure
python3 tools/todo/worker.py list          # branch, cleanliness, session of each; --json [--store] [--activity] [--usage]: the panel
```

`create` makes the folders; `list` shows each worker's branch, whether its tree is clean and its session. The editor panel "Todo Workers" shows the same in VS Code.

Two entries that would change the same file wait for each other: their branches would conflict at the merge, and the merge tool does not resolve conflicts (`.claude/skills/todo-loop/SKILL.md`, "Parallel workers"). The loop compares the files each taken entry touches before it picks the next one. Your **main checkout** is then for trying a branch and for the merge. Do not edit files in a checkout while a loop runs in it: the loop works in that folder and starts only on a clean tree.

## When it stops

A stop is a `todo stop`: the status stays `in progress` and the note says why. There are two kinds.

- **A park**: the entry is the problem (the architect found a missing premise, the same failure three times, two reviews without `APPROVE`, the work grew beyond the entry, a conflict with the base). The note starts with `parked:` and the loop goes on to the next entry. You read the reason (`todo.py show <id>`), then fix the entry, or answer the `Question:` entry it filed, and start it again with `/todo-loop <id>`, which continues an entry that is `in progress` on its branch.
- **A pause**: the machine or the store is the problem (a red baseline, a dirty tree, the store not answering, the same environment failure twice). In a worker the queue is paused and nothing starts after it until you resume it with `worker.py queue <k> --resume`. Fix the cause first.

## What is yours

The agents never `git push`, never `git fetch`, never merge into the base branch, never edit a vendored folder or the base itself, and never decide a licence, a product question or a change of a public format that the entry did not ask for. When one of those comes up, the loop stops and files a question for you (the list is the loop's `Forbidden` section and "Small decisions"). Yours are:

- the **review** of the branch (the page [What to review in an agent's work](review-agent-work.md)), and trying it live;
- the **merge**: `/todo-merge` prints a report, asks whether to merge (`Merge + close + delete branch`, `Merge only`, or `No`), merges with a merge commit, and after the full answer asks which version bump (when the repository keeps a changelog), closes the entry, files the review's follow-ups, deletes the branch and may ask whether to run the fast perf set; it never pushes;
- the **push**, the **access** to the store and the **priorities**.

## Start it, stop it, send it back

These write to the store or start sessions, so the page shows them and does not run them.

<!-- illustration: commands of the skills todo-loop and todo-loop-parallel and of tools/todo/CLAUDE.md; shown, not run on this page, because they start loops and write to the store -->
```text
/todo-loop <id>                              in a Claude Code session: take this entry
python3 tools/todo/worker.py start           a loop in every worker folder, in the background
python3 tools/todo/worker.py stop            stop those sessions (the conversations are kept)
```

A loop needs every repository clean and the store answering (`.claude/skills/todo-loop/SKILL.md`, "Preconditions"); it stops with a message when one fails.

When a branch in `review` is not right, write what is wrong in a file and send the branch back:

<!-- illustration: the command of tools/todo/CLAUDE.md with a made-up file name; shown, not run on this page, because it writes to the store -->
```text
python3 tools/todo/worker.py remarks <id> --file remarks.md
```

The same is the panel's "Return with remarks". The entry becomes `in progress` again, a developer does exactly those remarks (commits `Remarks fixes: <what>`), a check follows, and it comes back to `review`.

## What you did

You saw the roles of the loop and what each leaves behind, the statuses an entry goes through and the commands that show them, how a big entry is cut and how workers run several loops at once, what a park and a pause are, and which steps are only yours: the review, the merge, the push, the access and the priorities.

Next: [The skills](skills.md)
