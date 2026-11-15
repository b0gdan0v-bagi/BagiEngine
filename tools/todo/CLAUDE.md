# tools/todo

The todo store: the source of truth for the todo entries of every area (the engine, each game), their status, the roadmap stages, the plans and the reviews. It is a service on Railway (FastAPI over Postgres, `service/`) with a command line (`todo.py`). **Nothing of it lives in git** (no entry files, status commits or plans). The service mirrors every entry into Vikunja by itself and takes back what a person changes there, so nobody runs a push or a pull.

```
todo.py            the CLI (stdlib only): list, show, add, edit, claim, review, stop, close, dep, plan, area, import, export, admin
todo_markdown.py   shared (stdlib): the old entry-file format (import), checklists, summaries, markdown <-> HTML
worker.py          the worker folders of parallel todo-loop runs (stdlib; git worktrees, below)
service/           the service: app.py (API and pages), store.py (reads and writes), db.py (pool, migrations),
                   schema/NNN_*.sql, mirror.py (Vikunja mirror), vikunja.py (client, task rendering), dashboard.py (pages)
Dockerfile, service/requirements.txt   the image on Railway (build context: this folder)
.railway/railway.ts, package.json   the settings of the service on Railway (IaC; package.json only pins the SDK)
tests/             pytest: the API, the mirror (fake_vikunja.py), the CLI (uvicorn in a thread), the markdown
```

## Settings

The CLI reads `TODO_API_URL` (the service, no `/api/v1`) and `TODO_API_TOKEN` from the environment, then `--env-file`, then the first `.env` upwards that has `TODO_API_URL` (git-ignored; `.env.example`). `TODO_ACTOR` (or `--actor`) names the writer in the history (`todo-loop`, `owner`, ...; default `cli`). The token is never printed. Every accepted write (not `GET`) also rewrites the signal file `TODO_CHANGED_FILE` (default `~/.claude/todo/changed.json`: `at`, `actor`, `method`, `path`; by a rename; a failed signal never fails the command), so a viewer (`tools/vscode/todo-workers`) reads the store again at once.

The service reads `DATABASE_URL`, `TODO_API_TOKEN`, `VIKUNJA_URL`, `VIKUNJA_TOKEN` (the mirror is off without the two), `VIKUNJA_WEBHOOK_SECRET`, `TODO_PUBLIC_URL` (default `https://$RAILWAY_PUBLIC_DOMAIN`; the target of the webhooks) and `TODO_RECONCILE_SECONDS` (300).

## The data

- **Area** (`todo.py area set <id> --title T --vikunja-project P --roadmap roadmap.md`): one todo list and its Vikunja project, with the roadmap stages. `engine` (project `BagiEngine`) and one per game.
- **Entry**: `id` (letters, digits, `_`, `-`; unique over every area), `area`, `title`, `body` (markdown, no title line), `status` (`open`, `in progress`, `review`, `deferred`, `closed`), `statusDate`, `statusNote`, `branch` (`todo/<branch-id>` while taken), `base` (the base branch the branch was cut from), `origin` (the checkout that made it, `<game repository>@<host>`), `stage`, `plan` (the owner's plan), `version` (grows on every change; `edit --expected-version` refuses a stale write), `aliases` (old ids; a Vikunja marker with one still links), `slicing` (`through`, the default of `add`: one branch, one merge, `todo-loop/SKILL.md` "Through"; null: each slice waits for its merge), `costCap` (the dollars a through run may spend, null for none), `priority` (`now`, `high`, `normal`, `low` or null, which orders as `normal`: the owner's, an agent may propose one, `history` says who; independent of `stage` and not inherited through dependencies; `GET /entries?priority=now,high,none`). A new `schema/NNN_*.sql` runs when the service starts (the next deploy: the owner's step). `claim --base` sets `base` and `origin` (null for an older claim and without `--base`); they go with the branch: `close`, an API patch `clear: ["branch"]` and a change in Vikunja that opens or closes the entry (deleting its task too) clear all three. A finished entry is `closed`, not deleted. `statusLine` reads `review 2026-11-06 branch todo/x: what was done`. The dashboard's summary is the `**What is missing**` / `**What is wrong**` paragraph of the body.
- **Dependency** (`schema/004_dependencies.sql`): "`A` is blocked by `B`" with an optional note, between any two entries of any areas. The store refuses a link to itself, an unknown id and a cycle (409 naming it). An entry is `blocked` while one of its blockers is not `closed`; a closed blocker keeps its link as history. Every entry's JSON carries `blockedBy`, `blocks` (each `{id, status, title, note}`) and `blocked`; `GET /entries?blocked=true|false` filters. The loop takes only unblocked entries (`todo-loop/SKILL.md`, step 1), except one whose only open blocker waits in `review` on a branch of this checkout: it is stacked on, or paired with, that branch ("Stacked branches", "Paired"). So a link is a wait for the blocker's work, not for its merge; a part that can be done before the blocker is a note in the body, not a link.
- **Plan**: a markdown text with a bare file name (`todo-loop-<branch-id>.md`), a kind (`owner`, `slice`, `review`, `stages`: the stage plan of a whole through entry, `remarks`: the owner's remarks a review branch goes back with) and usually an entry. Its progress is its `- [ ]` / `- [x]` lines outside code fences. An entry shows its owner's plan, or else its newest slice plan; its JSON also sums every slice plan (`slices`: `count`, `done`, `total`) and names its stage plan (`stagesPlan`).
- **Events**: every write with its actor; `todo.py history <id>`.

## Plans never go into git

Agents read and write plans as files, so a plan has a local working copy in `~/.claude/plans/` (outside every repository) and its truth in the store. `plan get` writes and `plan put` reads a file **only outside a git work tree** (else it refuses), and so does `export`. The repositories ignore `.claude/plans/`, `*.plan.md` and `todo-export/` as a second guard, and the reviewer of the loop blocks a commit that holds a plan, a review or an entry file.

## Commands

```bash
python3 tools/todo/todo.py ping                                   # the service, the token, the mirror and its outbox
python3 tools/todo/todo.py list [--area A] [--status S,..] [--priority P,..] [--all] [--json]  # sorted by priority within an area
python3 tools/todo/todo.py show <id> [--json]                     # the entry as markdown
python3 tools/todo/todo.py add <id> --area A --title T [--stage N] [--priority P] [--slicing through|none] [--body-file F|-]
python3 tools/todo/todo.py edit <id> [--title] [--body-file F|-] [--stage N|none] [--priority P|none] [--plan NAME|none] [--status S] [--slicing through|none] [--cost-cap D|none] [--expected-version V]
python3 tools/todo/todo.py note <id> --note "..."                 # the status note alone (a through slice approved)
python3 tools/todo/todo.py claim <id> --branch todo/<branch-id> [--base B] [--origin O]  # open -> in progress (409 when taken: one winner); B: the base it is cut from, O: by default `<game repository>@<host>` with B
python3 tools/todo/todo.py review <id> --branch todo/<branch-id> --note "what was done, how it was checked"
python3 tools/todo/todo.py stop <id> --reason "..."               # stays in progress, "stopped: ..."
python3 tools/todo/todo.py close <id> [--reopen]                  # after the merge; --reopen: the next slice
python3 tools/todo/todo.py plan get|put|list|url|delete ...       # files in ~/.claude/plans/ by default
python3 tools/todo/todo.py history <id>
python3 tools/todo/todo.py dep add <id> <blocker>... [--note N] # <id> waits until each blocker is closed; `dep rm` takes it back
python3 tools/todo/todo.py dep list [<id>]                       # every link, or the blockers and the blocked of one entry
python3 tools/todo/todo.py dep graph [--area A]                  # the address of the graph page
python3 tools/todo/todo.py list --ready | --blocked              # every blocker closed, or not
python3 tools/todo/todo.py dashboard [--area A]                   # the address of the pages (Basic auth: the token)
python3 tools/todo/todo.py admin reconcile|webhooks|outbox        # the mirror by hand
python3 tools/todo/todo.py admin dedupe [--apply]                 # several open tasks for one entry: keep the oldest
python3 tools/todo/todo.py import --area A --todo DIR [--dry-run] # the old entry files and their plans (once; file name = id, an old `id:` = alias)
python3 tools/todo/todo.py export --out DIR                       # a backup outside git, old file format
```

Exit codes: 0, 1 (settings, network, a guard), 2 (the store refused: 404, 409, 422). `--json` works before or after the command.

## The mirror (service/mirror.py)

- **Store to Vikunja:** a write that changes what a task shows queues the entry in `vikunja_outbox` (one row per entry). The worker thread writes due rows every 2 s (at once after an API write), retries a failure with a backoff (5 s doubling, at most 10 min) and keeps what it wrote as the entry's `vikunja_snapshot`. A task shows the marker `todo-sync:id=<id>`, the status line, the stage and the plan, a rule, the body (markdown as HTML) and the plan's checklist as the editor's task list (capped). Owned: title, description, done and the labels `in progress`, `review`, `deferred`, `stage N`, the priority (`now` 5, `high` 3, `normal` 2, `low` 1, unset 0, compared by level: a task at 4 is `now` and stays 4); due date, assignees, comments and other labels are kept (read-modify-write).
- **Vikunja to store:** a webhook per project (`task.created`, `task.updated`, `task.deleted`; HMAC-SHA256 of the body in `X-Vikunja-Signature`, installed at start or by `admin webhooks`) and a reconcile pass every `TODO_RECONCILE_SECONDS`. The task is read again; what differs from the snapshot was a person: done closes the entry (undone opens it), the title, the body (HTML back to markdown, so formatting the editor cannot hold may change), the labels `in progress` / `deferred` (status) and `stage N`, the priority, a move to the project of another area. A snapshot without a `priority` key (older) adopts the task's priority when the entry has none (never wiped), else writes the store's. `review` never comes from Vikunja. A task without the marker, not done, becomes a new entry (id from the title). A deleted task closes its entry for good.
- **Dependencies** go one way, store to Vikunja: an entry's blockers are its task's `blocked` relations (Vikunja adds `blocking` to the other task). Only relations between tasks of entries are owned; a relation changed in Vikunja is put back at the next write of the entry. A refused relation (a token without `tasks_relations`) never fails the task: it is kept as `relationsError` of `/healthz` (`ping`) until a relation is written again.
- **One task per entry:** before the worker makes a task it looks for an unlinked task of the project whose marker names the entry (its id or an alias) and takes that one.
- **No lost edit, no echo:** the entry row is locked while its task is written and while a webhook is applied, so the echo of the mirror's own write meets the new snapshot and changes nothing; an edit that comes before a pending write is applied first, not overwritten. A task that lost its marker is still found by its id and gets the marker back. **Lock order everywhere: the entry row, then its outbox row** (table in `mirror.py`), so a write and the mirror never wait in a circle. A transaction Postgres still aborts (deadlock, serialization failure) is run again by the API up to four times, then 503; any other unexpected error is 500 with its class (traceback in the log).
- Health: `GET /healthz` (`todo.py ping`) shows the mirror, its last error and the outbox; `admin outbox` lists what waits and why.

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
python3 tools/todo/worker.py merge <id> [--dry-run] [--no-kick]  # quick merge of an entry in review, then `kick`
python3 tools/todo/worker.py kick [--merged id]      # idle workers take the head of their queues, detached
python3 tools/todo/worker.py update [k ...]          # detach an idle, clean worker at the tips of its bases (no build, no submodule update)
python3 tools/todo/worker.py remove 3      # a clean folder (its branches stay)
python3 tools/todo/worker.py queue [k] [--add id [--first] [--by-loop] | --remove id | --pop | --clear | --pause [--reason R] | --resume]  # (below)
python3 tools/todo/worker.py overlap <id>  # busy entries whose plans name its files
python3 tools/todo/worker.py prefs [--prefer a,b] [--avoid c] [--clear]   # the owner's preferences of /todo-dispatch
python3 tools/todo/worker.py dispatch --json [k ...] [--depth N]  # one read for /todo-dispatch; --record <file> keeps a classifier answer
python3 tools/todo/worker.py build-info    # preset, product, tests, screenshot tool (the panel's Verify, Build & run, Shots)
python3 tools/todo/worker.py cost <id> [--json]      # dollars of the loop sessions that held the entry, against its costCap
python3 tools/todo/worker.py remarks <id> --file F|- [--start]  # an entry in review goes back with the owner's remarks
python3 tools/todo/worker.py doctor [k] [--fix K] | merge-train [--dry-run] | busy | day | timeline | review-stats | machine  # the panel's (worker_insights.py)
```

- **Worktrees, not clones:** one shared `.git`, so a worker's branch is at once in the main checkout (tried and merged there) and in the other workers. A worker stands detached at the base: the main checkout holds the base branch.
- **In a game:** the worker's `engine/` is a worktree of the main checkout's engine module. **Never run `git submodule update` in a worker**: it would write the worker's path into the shared `core.worktree` and break the main `engine/`; `create` sets `submodule.engine.update=none` per worker, `check --fix` repairs `core.worktree`.
- **Shared by symlink:** `.env`, `.claude/settings.local.json`, `.venv` / `engine/.venv`.
- **Windows symlinks:** a tracked link whose target was missing at checkout (the game's `.claude/skills/*` into an older `engine/`) becomes a *file* symlink (`WinError 5`, the skill does not load); `create`, `update` and `start` remake it as a directory symlink, `check --fix` by hand.
- **Builds:** each worker has its own `build/`. `create` (`-D`) and every session (`build_env`, `setdefault`: the owner's wins) set `ENGINE_CCACHE_BASE_DIR=<the parent of the main checkout>`: every checkout and preset shares hits. The main checkout needs `base_dir = <that folder>` and `hash_dir = false` once in `~/.ccache/ccache.conf` (it wins) or the variable in the shell profile.
- **Sessions:** `start` runs `/todo-loop N` as a `claude --bg` session on a cheap model only in workers whose session has finished, `--delay` apart. First it detaches the worker at its bases' tips (as `update`; uncommitted changes: starts where it stands, with a note): an old game commit beside a newer engine base gives a red baseline. `list` shows a session waiting for a permission (a run that ended its turn with text is finished, `endedTurn`; `start` stops it first), one stopped by the usage limit (`relaunch` after the reset) and one stopped by `stop` (`~/.claude/todo/stopped-sessions.json`). A folder Claude Code does not trust yet needs one `run <k>` first.
- **Queues.** `start --queued-only` takes the first *takeable* entry of the worker's queue (`queue_wait`: the loop's stacked/paired rule from the store JSON, busy files left to the loop; blocked ones stay). A paused queue (`queue --pause`, set by every stopped loop; `--resume`) starts nothing; a new entry beyond `QUEUE_STREAK_LIMIT` in a row pauses it instead, counted anew on `--add` (not `--by-loop`), `--resume`, `--clear`, `remarks` and a merge of an entry the worker held. `--wait-free S` waits for the session to end, `--detach` runs it apart: every run in a worker ends so (`todo-loop/SKILL.md`, "End of the run"); `kick` (and `merge`) does it for each idle worker (finished, not stopped, clean, takeable head). Its state is the key `state` of `worker-queues.json`, beside the older lists. `remarks` stores the plan `todo-loop-<branch-id>-remarks-<n>.md` (kind `remarks`), sets the entry `in progress` and queues it first on the worker holding its branch (or a free one).
- **Quick merge** = `/todo-merge` after the owner's yes: refuses an entry not in review or with an open blocker, a foreign or merged branch, conflicts, a dirty tree; merges `--no-ff`, closes (or `--reopen`s) the entry, detaches the worker, deletes the branch, commits the engine pointer in a game, then `kick`s. It never builds and never pushes.
- **Branches of another checkout:** branches are never pushed, so an entry is reviewed and merged only where it was taken. `claim --base` stores `base` and `origin`; `list --store` marks entries whose branch is not here (`local` false) or was cut from another base (`foreignBase`), and the panel and `merge` refuse them.
- **The panel's files** (outside every repository, in `~/.claude/todo/`): `worker-queues.json`, `dispatch-prefs.json` and `dispatch-verdicts.json` (by the workers' root: the queues, the owner's preferences, the cached verdicts of `/todo-dispatch` while the entry's `version` holds, never a skip that depends on other entries) and `usage-ledger.json` (the tokens of each loop session by the entry its worker held). `list --store` also checks each local review branch against the base and the other ones.

## Railway

One Railway project holds Vikunja (Docker image `vikunja/vikunja:2.7.1`, SQLite on a volume), **Postgres** (the Railway template) and **todo-api** (this folder, `railway up tools/todo --path-as-root --service todo-api` from the engine root). Variables of todo-api: `DATABASE_URL=${{Postgres.DATABASE_URL}}`, `TODO_API_TOKEN` and `VIKUNJA_WEBHOOK_SECRET` (each `openssl rand -hex 32`), `VIKUNJA_URL` (the public Vikunja domain), `VIKUNJA_TOKEN` (a scoped token: `projects` create/read_all, `tasks` create/read_all/update/delete, `labels` create/read_all, `tasks_labels` update_bulk, `tasks_relations` create/delete, `projects_webhooks` create/read_all). A public domain on port 8080 (`PORT` is set by Railway). One replica: the worker runs in the web process. Postgres backups are the Railway volume's; `todo.py export` is one by hand.

**The settings of todo-api** (builder, health check, restart policy, the one replica and its region) are Railway Infrastructure as Code: `.railway/railway.ts` here, the partial `todo`, which owns only todo-api, so `apply` never touches Vikunja, Postgres or a game's web service; variables stay unmanaged (`railway variables`). A change: `cd tools/todo && npm install && railway config plan`, then `railway config apply` (after `railway link`).

## Tests

```bash
uv pip install --python .venv/bin/python -r tools/todo/requirements-dev.txt   # once
.venv/bin/python -m pytest tools/todo/tests
```

The service tests need a real Postgres: `TODO_TEST_DATABASE_URL` (wiped), else docker starts a `postgres:17-alpine` for the session; without either they skip. `fake_vikunja.py` stands in for the Vikunja 2.7.1 behaviour the mirror relies on.
