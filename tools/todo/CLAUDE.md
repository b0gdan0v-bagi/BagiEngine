# tools/todo

The command line of the **todo store**: the source of truth for the todo entries of every area (the engine, each game), their status, the roadmap stages, the plans and the reviews. The store, its service and its pages live in the owner's private BagiTodo repository and its instance; **nothing of it is in git here** (no entry files, status commits or plans). This folder holds only the **loader** of the command line, which the service hands out:

```
bagitodo_cli.py   the loader (stdlib, Python 3.10+): asks the service for the current version, downloads and checks it, runs it
todo.py           three lines: bagitodo_cli.run("todo.py")    -- the entries, plans, dependencies, areas
worker.py         three lines: bagitodo_cli.run("worker.py")  -- the worker folders of parallel todo-loop runs
buildslots.py     three lines: bagitodo_cli.run("buildslots.py")  -- the machine-wide allocator of C++ build jobs
```

The files are copied as they are from the loader of BagiTodo (`loader/` there) and are public code: no address, no token, no private name. Skills, rules, agents and `tools/vscode/todo-workers` call `tools/todo/todo.py` and `tools/todo/worker.py` as scripts only; never import or read anything else of this folder.

## The loader

Each run asks the service `GET /api/v1/cli?layout=tree` for the version it hands out (the relative paths and sha256 of its files, subfolders included; a path that leaves the version's folder is refused before any download; an answer without the tree layout is taken as a flat list of names), downloads a version it has not seen (`GET /api/v1/cli/<path>?layout=tree`) into the cache, checks every hash, runs a smoke check of the new version (`python -I`, every file imported, a file of a package by its dotted name, 15 s; a failure keeps `current` on the old version with one warning and retries later) and runs the script with the same arguments. `BAGITODO_CLI_PIN=<version>` runs that version from the cache without asking the service (downloaded only when the service hands out exactly that one, else an error). The answer is trusted for a minute, so a burst of calls asks once. When the service does not answer, the last version that ran is used, with a line on stderr; with none yet, it is an error. Versions unused for two weeks are removed. Updating the loader itself is a copy of BagiTodo's `loader/` again.

## Settings

- `TODO_API_URL` (the service, no `/api/v1`) and `TODO_API_TOKEN`: from the environment, then `--env-file`, then the first `.env` from the current folder upwards that has `TODO_API_URL` (git-ignored; `.env.example`). The token is the checkout's own, limited to its areas, and issued by the owner; it is never printed or committed.
- `BAGITODO_CACHE`: the cache of the downloaded versions (default `~/.cache/bagitodo`, readable by its owner only: it holds code that runs).
- `TODO_ACTOR` (or `--actor`) names the writer in the history (`todo-loop`, `owner`, ...; default `cli`).
- Every accepted write also rewrites the signal file `TODO_CHANGED_FILE` (default `~/.claude/todo/changed.json`), so a viewer (`tools/vscode/todo-workers`) reads the store again at once.

## The data

- **Area** (`todo.py area list|set`): one todo list with its roadmap stages. `engine` and one per game. An area is a setting of the shared store: changing it is the owner's step.
- **Entry**: `id` (letters, digits, `_`, `-`; unique over every area), `area`, `title`, `body` (markdown, no title line), `status` (`open`, `in progress`, `review`, `deferred`, `closed`), `statusNote`, `branch` (`todo/<branch-id>` while taken), `base` and `origin` (set by `claim --base`: the base branch and the checkout that made the branch; they go with the branch), `stage`, `plan` (the owner's plan), `version` (`edit --expected-version` refuses a stale write), `slicing` (`through`, the default of `add`: one branch, one merge, `todo-loop/SKILL.md` "Through"; none: each slice waits for its merge), `costCap`, `priority` (`now`, `high`, `normal`, `low` or unset, which orders as `normal`; the owner's, an agent may propose one). A finished entry is `closed`, not deleted. The summary is the `**What is missing**` / `**What is wrong**` paragraph of the body.
- **Dependency**: "`A` is blocked by `B`" with an optional note, between any two entries of any areas. The store refuses a link to itself, an unknown id and a cycle (409). An entry is `blocked` while one of its blockers is not `closed`; a closed blocker keeps its link as history. Every entry's JSON carries `blockedBy`, `blocks` and `blocked`. The loop takes only unblocked entries (`todo-loop/SKILL.md`, step 1), except one whose only open blocker waits in `review` on a branch of this checkout: it is stacked on, or paired with, that branch ("Stacked branches", "Paired"). So a link is a wait for the blocker's work, not for its merge; a part that can be done before the blocker is a note in the body, not a link.
- **Plan**: a markdown text with a bare file name (`todo-loop-<branch-id>.md`), a kind (`owner`, `slice`, `review`, `stages`: the stage plan of a through entry, `remarks`: the owner's remarks a review branch goes back with) and usually an entry. Its progress is its `- [ ]` / `- [x]` lines outside code fences.
- **Events**: every write with its actor; `todo.py history <id>`.

## Plans never go into git

Agents read and write plans as files, so a plan has a local working copy in `~/.claude/plans/` (outside every repository) and its truth in the store. `plan get` writes and `plan put` reads a file **only outside a git work tree** (else it refuses); `plan put` takes the bare name of a file in `~/.claude/plans/`. The repositories ignore `.claude/plans/`, `*.plan.md` and `todo-export/` as a second guard, and the reviewer of the loop blocks a commit that holds a plan, a review or an entry file.

## Commands

`todo.py --help`, `worker.py --help` and `<command> --help` give the details.

```bash
python3 tools/todo/todo.py ping                                   # the service and the token
python3 tools/todo/todo.py list [--area A] [--status S,..] [--priority P,..] [--all] [--ready|--blocked] [--json]
python3 tools/todo/todo.py show <id> [--json]                     # the entry as markdown
python3 tools/todo/todo.py add <id> --area A --title T [--stage N] [--priority P] [--slicing through|none] [--body-file F|-]
python3 tools/todo/todo.py edit <id> [--title] [--body-file F|-] [--stage N|none] [--priority P|none] [--plan NAME|none] [--status S] [--slicing through|none] [--cost-cap D|none] [--expected-version V]
python3 tools/todo/todo.py note <id> --note "..."                 # the status note alone (a through slice approved)
python3 tools/todo/todo.py claim <id> --branch todo/<branch-id> [--base B]  # open -> in progress (409 when taken: one winner)
python3 tools/todo/todo.py review <id> --branch todo/<branch-id> --note "what was done, how it was checked"
python3 tools/todo/todo.py stop <id> --reason "..."               # stays in progress, "stopped: ..."
python3 tools/todo/todo.py close <id> [--reopen]                  # after the merge; --reopen: the next slice
python3 tools/todo/todo.py plan get|put|list|url|delete ...       # files in ~/.claude/plans/ by default
python3 tools/todo/todo.py history <id>
python3 tools/todo/todo.py dep add|rm|list|graph ...              # <id> waits until each blocker is closed
python3 tools/todo/todo.py dashboard [--area A]                   # the address of the pages
python3 tools/todo/todo.py export --out DIR                       # a backup outside git, old file format
python3 tools/todo/todo.py version get [--counter C]              # the counter and the numbers it gave
python3 tools/todo/todo.py version next --bump minor|patch --entry <id> [--counter C]  # prints the number alone
python3 tools/todo/todo.py version set <v> --expected <old|none>  # the owner's token only
```

Exit codes: 0, 1 (settings, network, a guard), 2 (the store refused: 404, 409, 422). `--json` works before or after the command.

**The version counter** (`engine-version` by default) numbers the engine over every `games/*` branch, PC and worker, so no two entries share a number. `next` is atomic and keyed by the entry: a repeat with the same bump returns the same number, another bump for that entry is a 409. A number given is never taken back, so `next` is never run to try it out; a forecast reads `get`. `minor` resets PATCH to 0; MAJOR moves only by the owner's `set`. On any non-zero exit there is no number and none is made up locally. `tools/changelog/CLAUDE.md` says how a number reaches the changelog.

## Workers (worker.py)

Several loops at once, each in a **worker folder** (the loop's own rules: `.claude/skills/todo-loop/SKILL.md`, "Parallel workers"). From the main checkout:

```bash
python3 tools/todo/worker.py create 3      # <parent>/<name>.workers/w1..w3; --base, --preset, --no-configure
python3 tools/todo/worker.py list          # branch, cleanliness, session of each; --json [--store] [--activity] [--usage]: the panel
python3 tools/todo/worker.py check [--fix] # the shared config and the Windows links (below) of every worker
python3 tools/todo/worker.py start         # /todo-loop in every worker, background, --delay apart; --slices, --report-to, --print
python3 tools/todo/worker.py start 2 --detach --wait-free 1800 --queued-only --delay 0  # the end of a run in a worker (below)
python3 tools/todo/worker.py stop [k ...]  # stop background sessions (the conversation is kept)
python3 tools/todo/worker.py relaunch [k]  # resume sessions stopped by the usage limit
python3 tools/todo/worker.py attach <k>    # open the session of a worker (answer what it waits for)
python3 tools/todo/worker.py merge <id> [--dry-run] [--no-kick]  # quick merge of an entry in review, then `kick`
python3 tools/todo/worker.py kick [--merged id]      # idle workers take the head of their queues, detached
python3 tools/todo/worker.py update [k ...]          # detach an idle, clean worker at the tips of its bases (no build, no submodule update)
python3 tools/todo/worker.py remove 3      # a clean folder (its branches stay)
python3 tools/todo/worker.py queue [k] [--add id [--first] [--by-loop] | --remove id | --pop | --clear | --pause [--reason R] | --resume]
python3 tools/todo/worker.py overlap <id>  # busy entries whose plans name its files
python3 tools/todo/worker.py prefs | dispatch --json [k ...]   # the owner's preferences and the one read of /todo-dispatch
python3 tools/todo/worker.py build-info | cost <id> | remarks <id> --file F|- [--start]
python3 tools/todo/worker.py doctor [k] [--fix K] | merge-train [--dry-run] | busy | day | timeline | review-stats | machine  # the panel's
```

- **Worktrees, not clones:** one shared `.git`, so a worker's branch is at once in the main checkout (tried and merged there) and in the other workers. A worker stands detached at the base: the main checkout holds the base branch.
- **In a game:** the worker's `engine/` is a worktree of the main checkout's engine module. **Never run `git submodule update` in a worker**: it would write the worker's path into the shared `core.worktree` and break the main `engine/`; `create` sets `submodule.engine.update=none` per worker, `check --fix` repairs `core.worktree`.
- **Shared by symlink:** `.env`, `.claude/settings.local.json`, `.venv` / `engine/.venv`.
- **Windows symlinks:** a tracked link whose target was missing at checkout (the game's `.claude/skills/*` into an older `engine/`) becomes a *file* symlink (the skill does not load); `create`, `update` and `start` remake it as a directory symlink, `check --fix` by hand.
- **Builds:** each worker has its own `build/`. `create` and every session set `ENGINE_CCACHE_BASE_DIR=<the parent of the main checkout>` (the owner's value wins), so every checkout and preset shares hits (`cmake/BagiCompilerCache.cmake`).
- **Sessions:** `start` runs `/todo-loop N` as a `claude --bg` session only in workers whose session has finished, `--delay` apart, after detaching the worker at its bases' tips (as `update`). `list` shows a session waiting for a permission, one stopped by the usage limit (`relaunch` after the reset) and one stopped by `stop`. A folder Claude Code does not trust yet needs one `run <k>` first.
- **Queues.** `start --queued-only` takes the first *takeable* entry of the worker's queue (the loop's stacked/paired rule; blocked ones stay). A paused queue (`queue --pause`, set by a worker-level stop of a loop, not by a park of one entry; `--resume`) starts nothing; after 4 entries in a row without the owner the queue pauses instead. `--wait-free S` waits for the session to end, `--detach` runs it apart: every run in a worker ends so (`todo-loop/SKILL.md`, "End of the run"); `kick` (and `merge`) does it for each idle worker. A loop run that claimed nothing ends with the line `NOT TAKEN <id|->: <reason>` (`todo-loop/SKILL.md`, "Stopping the whole loop"); `list --json --store` gives it per worker as `noClaim: {entry, reason}` (null when the run took something or is not finished), and the VS Code panel shows the worker as `took nothing` with that entry and reason. `remarks` stores the plan `todo-loop-<branch-id>-remarks-<n>.md` (kind `remarks`), sets the entry `in progress` and queues it first on the worker holding its branch.
- **Quick merge** = `/todo-merge` after the owner's yes: refuses an entry not in review or with an open blocker, a foreign or merged branch, conflicts, a dirty tree; merges `--no-ff`, closes (or `--reopen`s) the entry, detaches the worker, deletes the branch, commits the engine pointer in a game, then `kick`s. It never builds and never pushes.
- **Branches of another checkout:** branches are never pushed, so an entry is reviewed and merged only where it was taken. `claim --base` stores `base` and `origin`; `list --store` marks entries whose branch is not here or was cut from another base, and the panel and `merge` refuse them.
- **The panel's files** (outside every repository, in `~/.claude/todo/`): `worker-queues.json`, `dispatch-prefs.json`, `dispatch-verdicts.json`, `usage-ledger.json`, `stopped-sessions.json`, `auto-pick.json` (the ledger of automatic picking, written by the loop's conductor: today's picks and their dollars, the parked entries; deleting it clears the stop after two parks).
- **`/todo-loop 1 auto`** is the run that a worker with an empty queue starts at the end of a run: it chooses the next entry by the classifier only from the allowlist of the rules file (`## Automatic picking`, off by default) and ends with `NOT TAKEN` when a limit (review count, daily cap, two parks in a row) holds.
