# tools/todo

The todo store: the source of truth for the todo entries of every area (the engine, each game), their status, the roadmap stages, the plans and the reviews. It is a service on Railway (FastAPI over Postgres, `service/`) with a command line (`todo.py`, standard library only). **Nothing of it lives in git**: no entry files, no status commits, no plans. The service mirrors every entry into Vikunja by itself and takes back what a person changes there, so nobody runs a push or a pull.

```
todo.py            the CLI (stdlib only): list, show, add, edit, claim, review, stop, close, plan, area, import, export, admin
todo_markdown.py   shared (stdlib): the old entry-file format (import), checklists, summaries, markdown <-> HTML
worker.py          the worker folders of parallel todo-loop runs (stdlib; git worktrees, below)
service/           the service: app.py (API and pages), store.py (reads and writes), db.py (pool, migrations),
                   schema/NNN_*.sql, mirror.py (Vikunja mirror), vikunja.py (client, task rendering), dashboard.py (pages)
Dockerfile, railway.json, service/requirements.txt   the image on Railway (build context: this folder)
tests/             pytest: the API, the mirror (fake_vikunja.py), the CLI (uvicorn in a thread), the markdown
```

## Settings

The CLI reads `TODO_API_URL` (the service, no `/api/v1`) and `TODO_API_TOKEN` from the environment, then `--env-file`, then the first `.env` from the current folder upwards that has `TODO_API_URL` (git-ignored; `.env.example` shows the keys). `TODO_ACTOR` (or `--actor`) names the writer in the history (`todo-loop`, `owner`, ...; default `cli`). The token is never printed. Every write the store accepts (any method but `GET`) also rewrites the signal file `TODO_CHANGED_FILE` (default `~/.claude/todo/changed.json`: `at`, `actor`, `method`, `path`; a temporary file and a rename, and a signal that cannot be written never fails the command), so a viewer reads the store again at once: `tools/vscode/todo-workers` watches it.

The service reads `DATABASE_URL`, `TODO_API_TOKEN`, `VIKUNJA_URL`, `VIKUNJA_TOKEN` (the mirror is off without the two), `VIKUNJA_WEBHOOK_SECRET`, `TODO_PUBLIC_URL` (default `https://$RAILWAY_PUBLIC_DOMAIN`; the target of the webhooks) and `TODO_RECONCILE_SECONDS` (300).

## The data

- **Area** (`todo.py area set <id> --title T --vikunja-project P --roadmap roadmap.md`): one todo list and its Vikunja project, with the roadmap stages. Today `engine` (project `BagiEngine`) and `chess` (`BagiEngine-Chess`). It replaces the old pair `--todo` / `--project` that could be mixed up.
- **Entry**: `id` (letters, digits, `_`, `-`; unique over every area), `area`, `title`, `body` (markdown, no title line), `status` (`open`, `in progress`, `review`, `deferred`, `closed`), `statusDate`, `statusNote`, `branch` (`todo/<branch-id>` while taken), `base` (the base branch the branch was cut from) and `origin` (the checkout that made it, `<game repository>@<host>`): both are set by `claim --base`, are null for an entry claimed before they existed and are cleared with the branch by `close` (and by an API patch with `clear: ["branch"]`); `claim` without `--base` stores neither; the migration `schema/003_branch_origin.sql` that adds them runs when the service starts, i.e. with the next deploy (the owner's step), `stage`, `plan` (the owner's plan), `version` (grows on every change; `edit --expected-version` refuses a stale write), `aliases` (old ids: the `id:` an old entry file kept for the tracker; a Vikunja marker with one still links). A finished entry is `closed`, not deleted. `statusLine` spells it as the old front matter did: `review 2026-11-06 branch todo/x: what was done`. The summary on the dashboard is the `**What is missing**` / `**What is wrong**` paragraph of the body.
- **Plan**: a markdown text with a bare file name (`todo-loop-<branch-id>.md`), a kind (`owner`, `slice`, `review`) and usually an entry. Its progress is its `- [ ]` / `- [x]` lines outside code fences. An entry shows its owner's plan, or else its newest slice plan.
- **Events**: every write with its actor; `todo.py history <id>`. They replace the history the status commits used to give.

## Plans never go into git

Agents read and write plans as files, so a plan has a local working copy in `~/.claude/plans/` (outside every repository) and its truth in the store. `plan get` writes and `plan put` reads a file **only outside a git work tree** (`git rev-parse --show-toplevel` of the folder; it refuses otherwise), and so does `export`. The repositories ignore `.claude/plans/`, `*.plan.md` and `todo-export/` as a second guard, and the reviewer of the loop blocks a commit that holds a plan, a review or an entry file.

## Commands

```bash
python3 tools/todo/todo.py ping                                   # the service, the token, the mirror and its outbox
python3 tools/todo/todo.py list [--area A] [--status S,..] [--all] [--json]
python3 tools/todo/todo.py show <id> [--json]                     # the entry as markdown
python3 tools/todo/todo.py add <id> --area A --title T [--stage N] [--body-file F|-]
python3 tools/todo/todo.py edit <id> [--title] [--body-file F|-] [--stage N|none] [--plan NAME|none] [--status S] [--expected-version V]
python3 tools/todo/todo.py claim <id> --branch todo/<branch-id> [--base B] [--origin O]  # open -> in progress (409 when taken: one winner); B is the base the branch is cut from, O defaults to `<game repository>@<host>` when B is given
python3 tools/todo/todo.py review <id> --branch todo/<branch-id> --note "what was done, how it was checked"
python3 tools/todo/todo.py stop <id> --reason "..."               # stays in progress, "stopped: ..."
python3 tools/todo/todo.py close <id> [--reopen]                  # after the merge; --reopen: the next slice, open again
python3 tools/todo/todo.py plan get|put|list|url|delete ...       # files in ~/.claude/plans/ by default
python3 tools/todo/todo.py history <id>
python3 tools/todo/todo.py dashboard [--area A]                   # the address of the pages (Basic auth: any user, the token)
python3 tools/todo/todo.py admin reconcile|webhooks|outbox        # the mirror by hand
python3 tools/todo/todo.py admin dedupe [--apply]                 # several open tasks for one entry: keep the oldest, delete the rest
python3 tools/todo/todo.py import --area A --todo DIR [--dry-run] # the old entry files and their plans (once; the file name is the id, an old `id:` an alias)
python3 tools/todo/todo.py export --out DIR                       # a backup outside git, in the old file format
```

Exit codes: 0, 1 (settings, network, a guard), 2 (the store refused: 404, 409, 422). `--json` works before or after the command.

## The mirror (service/mirror.py)

- **Store to Vikunja:** a write that changes what a task shows queues the entry in `vikunja_outbox` (one row per entry). The worker thread writes due rows every 2 s (and at once after a write of the API), retries a failure with a backoff (5 s doubling, at most 10 min) and keeps what it wrote as the entry's `vikunja_snapshot`. A task shows the marker `todo-sync:id=<id>`, the status line, the stage and the plan, a rule, the body (markdown as HTML) and the plan's checklist as the editor's task list (at most 300 items of 240 characters). Owned: title, description, done and the labels `in progress`, `review`, `deferred`, `stage N`; priority, due date, assignees, comments and other labels are kept (read-modify-write).
- **Vikunja to store:** a webhook per project (`task.created`, `task.updated`, `task.deleted`; HMAC-SHA256 of the body in `X-Vikunja-Signature`, installed at start or by `admin webhooks`) and a reconcile pass every `TODO_RECONCILE_SECONDS`. The task is read again and compared with the snapshot; what differs was a person: done closes the entry (undone opens it), the title, the body (HTML back to markdown, so formatting the editor cannot hold may change), the labels `in progress` / `deferred` (status) and `stage N`, a move to the project of another area. `review` never comes from Vikunja. A task without the marker that is not done becomes a new entry (id from the title). A deleted task closes its entry, and no task is made again for it.
- **One task per entry:** before the worker makes a task it looks for an unlinked task of the project whose marker names the entry (its id or an alias) and takes that one. (Without it, the first import of 2026-11-06 made twelve duplicates when the areas got their project after the entries were imported; `admin dedupe --apply` removed them.)
- **No lost edit, no echo:** the entry row is locked while its task is written and while a webhook is applied, so the echo of the mirror's own write meets the new snapshot and changes nothing; an edit that arrives before a pending write is applied first and shown, not overwritten. A task that lost its marker (a stale browser tab) is still found by its id and gets the marker back. The marker also links the tasks the old `todo_sync push` made (reconcile, once).
- Health: `GET /healthz` (`todo.py ping`) shows the mirror, its last error and the outbox; `admin outbox` lists what waits and why.

## Workers (worker.py)

`/todo-loop` works in one checkout, one agent after the other; to work several entries at once, run several loops, each in a **worker folder** of its own (`.claude/skills/todo-loop/SKILL.md`, "Parallel workers", has the rules the loop adds: the base is reached detached, the busy files of the taken entries, the stub plan with `## Files` at the claim). From anywhere in the main checkout:

```bash
python3 tools/todo/worker.py create 3      # <parent>/<name>.workers/w1..w3, the missing ones; --base, --preset, --no-configure
python3 tools/todo/worker.py list          # the branch of each folder (and of its engine/), clean or not, its background session; --json [--store] [--activity] [--usage] for tools/vscode/todo-workers (the VS Code panel; --store adds the entries in progress and in review of the areas of `.claude/rules/todo-project.md` (its "Areas" table; all areas without one), each with `local` (its branch exists in the area's repository) and `foreignBase` (below, "Branches of another checkout"), and `elsewhere` lists the ids of those that are not this checkout's, --activity the last steps of each session and of its agents, --usage their tokens (in total, by model, in the last 10 min, the context now); both read from the transcripts in ~/.claude/projects)
python3 tools/todo/worker.py check [--fix] # the shared config of the engine module and every worker
python3 tools/todo/worker.py run 2         # cd w2 and start claude in this terminal (arguments after --; --print only prints)
python3 tools/todo/worker.py start         # /todo-loop in every worker as a background session, a minute apart; [k ...], --slices N, --loop-args, --report-to <session> (the loops send their reports there), --delay S, --print
python3 tools/todo/worker.py stop [k ...]  # stop the background sessions (the conversation is kept)
python3 tools/todo/worker.py remove 3      # a clean folder; its branches stay (they are refs of the shared .git)
python3 tools/todo/worker.py merge <id> [--dry-run] [--json]  # the quick merge of an entry in review (below)
python3 tools/todo/worker.py update [k ...] [--json]  # detach a finished, clean worker at the tips of its base branches (the panel's Update)
```

- **Worktrees, not clones:** every folder shares the one `.git`, so a branch made in a worker is at once in the main checkout (where the owner tries it and `/todo-merge` merges it, with no `fetch`) and in the other workers (which compare busy files with it). A new worker starts detached at `--base` (`main`): the main checkout holds the base branch, and a branch is checked out in one worktree only.
- **In a game** (a submodule `engine/`): the worker's `engine/` is a worktree of the main checkout's engine module (`.git/modules/engine`), detached at the game's pointer, so engine branches are shared too. A `git submodule update` in a worker's game repository would write the worker's path into the module's shared `core.worktree` and break the main `engine/`, so `create` sets `extensions.worktreeConfig` on the main repository and `submodule.engine.update=none` for the worker only (the update then says "Skipping submodule 'engine'"); `check` compares `core.worktree` with the main `engine/` and `check --fix` sets it back. The nested submodules (`external/*`) are cloned from the main checkout's modules (no network, hard links) with the URL given on the command line, never written to the shared config.
- **Shared with the main checkout** by symbolic links: `.env` (the store's token), `.claude/settings.local.json` (the permissions of Claude Code, so a yes in one session holds in all), `.venv` / `engine/.venv` (the reflection generator; a link is not matched by `.venv/`, so it goes into `info/exclude`). A new worker is checked clean before it is used.
- **Builds:** each worker has its own `build/`, and `create` runs its first configure (with the main checkout's venv on `PATH`; a failure is printed, the build skill configures again). No ccache: measured on the owner's machine (14 cores, 2026-11-07) a full build of the game takes about 17 s, and between worktrees ccache hit only 32 % (the PCH and the generated reflection code have absolute paths). `run` sets `CMAKE_BUILD_PARALLEL_LEVEL` to the cores divided by the number of workers, so three builds at once share the machine.
- **One command for all of them:** the skill `/todo-loop-parallel` (`.claude/skills/todo-loop-parallel/`) checks, creates the workers when there are none and calls `start`, which runs `claude --bg --name todo-w<k> '/todo-loop N'` in each worker (a real session in the background, the permission mode of the settings, `CMAKE_BUILD_PARALLEL_LEVEL` as for `run`), `--delay` seconds apart (60: two loops that choose at the same moment can take entries whose files meet; the claim only keeps them off one entry). A worker whose session has not finished (`claude agents --json`: a background session in the folder whose `state` is not `done`) is left alone, so `start` again restarts only the finished ones. A session that waits for a permission stops its loop until someone answers: `list` shows the state, `claude attach todo-w<k>` opens it, `claude agents` shows every session. A folder that Claude Code does not trust yet cannot start in the background: `run <k>` once and accept the prompt. `stop` stops a session; its loop leaves the entry `in progress` on its branch and the next loop in that worker continues it.
- **Quick merge** (`merge <id>`, the panel's button): what `/todo-merge` does after the owner's yes, without a Claude session. The area's repository and base branch come from the "Areas" table of `.claude/rules/todo-project.md` (an entry of another area is refused). It refuses an entry not in `review`, a branch that is not in the repository or was cut from another base (below), a branch already merged, conflicts (`git merge-tree`), uncommitted changes and a base branch checked out in another worktree, with nothing changed; otherwise it switches the repository to the base, merges `--no-ff` (`Merge branch '<branch>'`), runs `todo.py close <id>` (`--reopen` when the status note ends with "after the merge set the status back to open"; actor `quick-merge`), detaches the worker that holds the branch when it is clean, deletes the branch (`-d`) and, for the engine in a game, commits the pointer alone (`Update engine: <title>`). It never builds: `--dry-run` reports `moved` (the base moved since the branch was cut) and the owner runs the tests; `/todo-merge` builds in that case. Never pushes.
- **Branches of another checkout.** The store records an entry's `branch`, not where it was made, and any game's loop may take an entry of the area `engine` (its base is then `games/<that game>`), on any machine; the branches are never pushed. So `list --store` checks every entry against this checkout: `local` is false when the branch is not in the area's repository (taken in another checkout or on another machine), and `foreignBase` names another base branch the repository knows (`main` or `games/*`, local or fetched, not the area's base or a copy of it) whose commits the branch holds and the base has not: it was cut from there. Both make the panel grey out Review, Review with Claude, Quick merge and Merge with Claude, and the Todo list tab drop Attach to free worker; `merge` refuses both. `foreignBase` sees only what the repository has fetched: a branch of `games/chess` passes unnoticed when `origin/games/chess` is unknown here, and a branch that was merged into another base too looks foreign (the entry `todo-branch-origin` stores the base and the origin with the claim).
- **Update** (`update [k ...]`, the panel's button): "go to the base" of the loop, done by hand for an idle worker: every repository of the "Areas" table (the game first, then `engine/`) is detached at the tip of its base branch (`git switch --detach <base>`), so a worker that stayed on its last review branch builds the merged state. The branch it held is kept. Refused, with nothing changed, while the worker's session has not finished or a repository has uncommitted changes; a note says when the game's pointer of `engine/` is not the tip of `games/<game>` (a pointer commit not made yet). It never builds (the next build configures when the pointer moved) and never runs `git submodule update`.
- **Removing:** `git worktree remove` refuses a worktree with submodules, so `remove` deletes a clean folder and runs `git worktree prune` in both repositories.

## Railway

One Railway project holds Vikunja (Docker image `vikunja/vikunja:2.7.1`, SQLite on a volume), **Postgres** (the Railway template) and **todo-api** (this folder, `railway up tools/todo --path-as-root --service todo-api` from the engine root). Variables of todo-api: `DATABASE_URL=${{Postgres.DATABASE_URL}}`, `TODO_API_TOKEN` (`openssl rand -hex 32`), `VIKUNJA_URL` (the public Vikunja domain), `VIKUNJA_TOKEN` (a scoped token: `projects` create/read_all, `tasks` create/read_all/update/delete, `labels` create/read_all, `tasks_labels` update_bulk, `projects_webhooks` create/read_all), `VIKUNJA_WEBHOOK_SECRET` (`openssl rand -hex 32`). A public domain on port 8080 (`PORT` is set by Railway). One replica: the worker runs in the web process. Postgres backups are the Railway volume's; `todo.py export` is a backup by hand.

## Tests

```bash
uv pip install --python .venv/bin/python -r tools/todo/requirements-dev.txt   # once
.venv/bin/python -m pytest tools/todo/tests
```

The service tests need a real Postgres: `TODO_TEST_DATABASE_URL` (wiped), else a `postgres:17-alpine` container is started through docker for the session; without either they skip. `fake_vikunja.py` copies the Vikunja 2.7.1 behaviour the mirror relies on (full replace on update, paging headers, labels in the listing, 404 for a deleted task, project webhooks, `fail_next` for retries).
