# tools/todo

The todo store: the source of truth for the todo entries of every area (the engine, each game), their status, the roadmap stages, the plans and the reviews. It is a service on Railway (FastAPI over Postgres, `service/`) with a command line (`todo.py`, standard library only). **Nothing of it lives in git**: no entry files, no status commits, no plans. The service mirrors every entry into Vikunja by itself and takes back what a person changes there, so nobody runs a push or a pull.

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

The CLI reads `TODO_API_URL` (the service, no `/api/v1`) and `TODO_API_TOKEN` from the environment, then `--env-file`, then the first `.env` from the current folder upwards that has `TODO_API_URL` (git-ignored; `.env.example` shows the keys). `TODO_ACTOR` (or `--actor`) names the writer in the history (`todo-loop`, `owner`, ...; default `cli`). The token is never printed. Every write the store accepts (any method but `GET`) also rewrites the signal file `TODO_CHANGED_FILE` (default `~/.claude/todo/changed.json`: `at`, `actor`, `method`, `path`; a temporary file and a rename, and a signal that cannot be written never fails the command), so a viewer reads the store again at once: `tools/vscode/todo-workers` watches it.

The service reads `DATABASE_URL`, `TODO_API_TOKEN`, `VIKUNJA_URL`, `VIKUNJA_TOKEN` (the mirror is off without the two), `VIKUNJA_WEBHOOK_SECRET`, `TODO_PUBLIC_URL` (default `https://$RAILWAY_PUBLIC_DOMAIN`; the target of the webhooks) and `TODO_RECONCILE_SECONDS` (300).

## The data

- **Area** (`todo.py area set <id> --title T --vikunja-project P --roadmap roadmap.md`): one todo list and its Vikunja project, with the roadmap stages. Today `engine` (project `BagiEngine`) and `chess` (`BagiEngine-Chess`).
- **Entry**: `id` (letters, digits, `_`, `-`; unique over every area), `area`, `title`, `body` (markdown, no title line), `status` (`open`, `in progress`, `review`, `deferred`, `closed`), `statusDate`, `statusNote`, `branch` (`todo/<branch-id>` while taken), `base` (the base branch the branch was cut from), `origin` (the checkout that made it, `<game repository>@<host>`), `stage`, `plan` (the owner's plan), `version` (grows on every change; `edit --expected-version` refuses a stale write), `aliases` (old ids: the `id:` an old entry file kept for the tracker; a Vikunja marker with one still links), `slicing` (null, or `through`: the loop starts the next slice on the same branch as soon as the last one is approved and the owner merges once, `todo-loop/SKILL.md` "Through") and `costCap` (the dollars a through run may spend, null for none). A new `schema/NNN_*.sql` runs when the service starts, i.e. with the next deploy (the owner's step). `base` and `origin` are set by `claim --base`, are null for an entry claimed before they existed, and go with the branch: `close`, an API patch with `clear: ["branch"]` and a change made in Vikunja that opens or closes the entry (deleting its task there too) clear all three; `claim` without `--base` stores neither. A finished entry is `closed`, not deleted. `statusLine` spells it as the old front matter did: `review 2026-11-06 branch todo/x: what was done`. The summary on the dashboard is the `**What is missing**` / `**What is wrong**` paragraph of the body.
- **Dependency** (`schema/004_dependencies.sql`): "`A` is blocked by `B`" with an optional note, between any two entries of any areas. The store refuses a link to itself, an unknown id and a cycle (409 naming it). An entry is `blocked` while one of its blockers is not `closed`; a closed blocker keeps its link as history. Every entry's JSON carries `blockedBy`, `blocks` (each `{id, status, title, note}`) and `blocked`; `GET /entries?blocked=true|false` filters. The loop takes only unblocked entries (`todo-loop/SKILL.md`, step 1), except one whose only open blocker waits in `review` on a branch of the same repository: that one is stacked on the blocker's branch (`todo-loop/SKILL.md`, "Stacked branches"). So a link is a wait for the blocker's work, not for its merge; a part that can be done before the blocker is a note in the body, not a link.
- **Plan**: a markdown text with a bare file name (`todo-loop-<branch-id>.md`), a kind (`owner`, `slice`, `review`, `stages`: the stage plan of a whole through entry, `remarks`: the owner's remarks a review branch goes back with) and usually an entry. Its progress is its `- [ ]` / `- [x]` lines outside code fences. An entry shows its owner's plan, or else its newest slice plan; its JSON also sums every slice plan (`slices`: `count`, `done`, `total`) and names its stage plan (`stagesPlan`).
- **Events**: every write with its actor; `todo.py history <id>`.

## Plans never go into git

Agents read and write plans as files, so a plan has a local working copy in `~/.claude/plans/` (outside every repository) and its truth in the store. `plan get` writes and `plan put` reads a file **only outside a git work tree** (`git rev-parse --show-toplevel` of the folder; it refuses otherwise), and so does `export`. The repositories ignore `.claude/plans/`, `*.plan.md` and `todo-export/` as a second guard, and the reviewer of the loop blocks a commit that holds a plan, a review or an entry file.

## Commands

```bash
python3 tools/todo/todo.py ping                                   # the service, the token, the mirror and its outbox
python3 tools/todo/todo.py list [--area A] [--status S,..] [--all] [--json]
python3 tools/todo/todo.py show <id> [--json]                     # the entry as markdown
python3 tools/todo/todo.py add <id> --area A --title T [--stage N] [--body-file F|-]
python3 tools/todo/todo.py edit <id> [--title] [--body-file F|-] [--stage N|none] [--plan NAME|none] [--status S] [--slicing through|none] [--cost-cap D|none] [--expected-version V]
python3 tools/todo/todo.py note <id> --note "..."                 # the status note alone (a through slice approved, the status stays)
python3 tools/todo/todo.py claim <id> --branch todo/<branch-id> [--base B] [--origin O]  # open -> in progress (409 when taken: one winner); B is the base the branch is cut from, O defaults to `<game repository>@<host>` when B is given
python3 tools/todo/todo.py review <id> --branch todo/<branch-id> --note "what was done, how it was checked"
python3 tools/todo/todo.py stop <id> --reason "..."               # stays in progress, "stopped: ..."
python3 tools/todo/todo.py close <id> [--reopen]                  # after the merge; --reopen: the next slice, open again
python3 tools/todo/todo.py plan get|put|list|url|delete ...       # files in ~/.claude/plans/ by default
python3 tools/todo/todo.py history <id>
python3 tools/todo/todo.py dep add <id> <blocker>... [--note N] # <id> waits until each blocker is closed; `dep rm` takes it back
python3 tools/todo/todo.py dep list [<id>]                       # every link, or the blockers and the blocked of one entry
python3 tools/todo/todo.py dep graph [--area A]                  # the address of the graph page (/dashboard/graph)
python3 tools/todo/todo.py list --ready | --blocked              # only entries with every blocker closed, or the others
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
- **Dependencies** go one way, store to Vikunja: an entry's blockers are its task's `blocked` relations (Vikunja adds `blocking` to the other task). Only relations between tasks of entries are owned; a relation changed in Vikunja is put back at the next write of the entry. A refused relation (a token without `tasks_relations`) never fails the task: it is kept as `relationsError` of `/healthz` (`ping`) until a relation is written again.
- **One task per entry:** before the worker makes a task it looks for an unlinked task of the project whose marker names the entry (its id or an alias) and takes that one.
- **No lost edit, no echo:** the entry row is locked while its task is written and while a webhook is applied, so the echo of the mirror's own write meets the new snapshot and changes nothing; an edit that arrives before a pending write is applied first and shown, not overwritten. A task that lost its marker is still found by its id and gets the marker back.
- Health: `GET /healthz` (`todo.py ping`) shows the mirror, its last error and the outbox; `admin outbox` lists what waits and why.

## Workers (worker.py)

Several loops at once, each in a **worker folder** (the loop's own rules: `.claude/skills/todo-loop/SKILL.md`, "Parallel workers"). From the main checkout:

```bash
python3 tools/todo/worker.py create 3      # <parent>/<name>.workers/w1..w3; --base, --preset, --no-configure
python3 tools/todo/worker.py list          # branch, cleanliness and session of each; --json [--store] [--activity] [--usage] for the VS Code panel
python3 tools/todo/worker.py check [--fix] # the shared config of the engine module and every worker
python3 tools/todo/worker.py run 2         # claude in w2 in this terminal
python3 tools/todo/worker.py start         # /todo-loop in every worker in the background, --delay apart; --slices, --report-to, --print
python3 tools/todo/worker.py start 2 --detach --wait-free 1800 --queued-only --delay 0  # a through loop's own next slice (below)
python3 tools/todo/worker.py stop [k ...]  # stop background sessions (the conversation is kept)
python3 tools/todo/worker.py relaunch [k]  # resume sessions stopped by the usage limit
python3 tools/todo/worker.py merge <id> [--dry-run]  # quick merge of an entry in review
python3 tools/todo/worker.py update [k ...]          # detach an idle, clean worker at the tips of its bases (no build, no submodule update)
python3 tools/todo/worker.py remove 3      # a clean folder; its branches stay
python3 tools/todo/worker.py queue [k] [--add id [--first] | --remove id | --pop | --clear]  # the panel's next entries per worker
python3 tools/todo/worker.py overlap <id>  # busy entries whose slice plans name files the entry names (a hint)
python3 tools/todo/worker.py prefs [--prefer a,b] [--avoid c] [--clear]   # the owner's preferences of /todo-dispatch
python3 tools/todo/worker.py dispatch --json [k ...] [--depth N]  # one read for /todo-dispatch; --record <file> keeps a classifier answer
python3 tools/todo/worker.py build-info    # preset, product, tests, screenshot tool (the panel's Verify, Build & run, Shots)
python3 tools/todo/worker.py cost <id> [--json]      # dollars of every loop session that held the entry, against its costCap
python3 tools/todo/worker.py remarks <id> --file F|- [--start]  # an entry in review goes back with the owner's remarks
```

- **Worktrees, not clones:** one shared `.git`, so a worker's branch is at once in the main checkout (tried and merged there, no `fetch`) and in the other workers. A worker starts detached at the base, because the main checkout holds the base branch.
- **In a game:** the worker's `engine/` is a worktree of the main checkout's engine module. **Never run `git submodule update` in a worker**: it would write the worker's path into the shared `core.worktree` and break the main `engine/`; `create` sets `submodule.engine.update=none` per worker, `check --fix` repairs `core.worktree`.
- **Shared by symlink:** `.env`, `.claude/settings.local.json` (one permission answer holds everywhere), `.venv` / `engine/.venv`.
- **Builds:** each worker has its own `build/`; `run` / `start` set `CMAKE_BUILD_PARALLEL_LEVEL` to cores / workers. `create` configures with `ENGINE_CCACHE_BASE_DIR=<the workers' folder>`, so the workers share ccache hits (`cmake/BagiCompilerCache.cmake`); a worker made before needs `cmake --preset <p> -DENGINE_CCACHE_BASE_DIR=<that folder>` once.
- **Sessions:** `start` runs `claude --bg --name todo-w<k> --model sonnet --effort medium '/todo-loop N'` (the conductor writes no code; `--model` / `--effort` change it) only in workers whose session has finished, `--delay` (60 s) apart so two loops do not choose at once. Before it starts a loop it detaches the worker at the tips of its bases (as `update`; a worker with uncommitted changes is started where it stands, with a note): the loop's own "go to the base" may miss a repository, and a game on an old commit beside a newer engine base gives a red baseline. `list` shows a session that waits for a permission (a pending tool call; a run that ended its turn with text and stays `blocked` in `claude agents` is finished, `endedTurn`, and `start` stops it before the next loop) (`attach <k>`: `claude attach` by the session's id; `logs <k>` the same), one stopped by the usage limit (`relaunch` after the reset) and one stopped by `stop` (marked in `~/.claude/todo/stopped-sessions.json`, because `claude agents` shows it as finished). A folder Claude Code does not trust yet needs one `run <k>` first.
- **Through and remarks.** `start --queued-only` starts a worker on the head of its queue (taken out; an empty queue: not started), `--wait-free S` first waits for the worker's session to end, `--detach` does it all in a process of its own. A through loop queues its entry first on its own worker and runs this as its last step, so the next slice gets a fresh conductor without the panel; whichever of it and the panel's auto-start comes second finds the queue empty or the worker busy. `cost` updates the usage ledger from the sessions now (a session counts for the entry whose branch its worker holds) and prices it (`~/.claude/todo/prices.json` replaces the default table). `remarks` stores the remarks as the plan `todo-loop-<branch-id>-remarks-<n>.md` (kind `remarks`), sets the entry back to `in progress` with the note `returned with remarks, round <n>` and queues it first on the worker that holds its branch (or a free one).
- **Quick merge** = `/todo-merge` after the owner's yes, without a session: refuses an entry not in `review`, one with an open blocker, a foreign or merged branch, conflicts, a dirty tree; merges `--no-ff`, closes (or `--reopen`s) the entry, detaches the worker, deletes the branch, commits the engine pointer in a game. It never builds and never pushes.
- **Branches of another checkout:** branches are never pushed, so an entry is reviewed and merged only where it was taken. `claim --base` stores `base` and `origin`; `list --store` marks entries whose branch is not here (`local` false) or was cut from another base (`foreignBase`), and the panel and `merge` refuse them.
- **The panel's files** (outside every repository, in `~/.claude/todo/`): `worker-queues.json` (by the workers' root), `dispatch-prefs.json` and `dispatch-verdicts.json` (by the workers' root: the owner's preferences and the classifier's verdicts of `/todo-dispatch`, a verdict reused while the entry's `version` is unchanged, a skip that depends on other entries never) and `usage-ledger.json` (the tokens of each loop session by the entry its worker held, so an entry's cost survives its sessions). `list --store` also checks each local review branch (`merge-tree` against the base and the other review branches, the base's commits since the cut and the files both changed), and `list` names the pending tool call of a waiting session (`asking`) and the engine pointer's state (`pointer`).

## Railway

One Railway project holds Vikunja (Docker image `vikunja/vikunja:2.7.1`, SQLite on a volume), **Postgres** (the Railway template) and **todo-api** (this folder, `railway up tools/todo --path-as-root --service todo-api` from the engine root). Variables of todo-api: `DATABASE_URL=${{Postgres.DATABASE_URL}}`, `TODO_API_TOKEN` (`openssl rand -hex 32`), `VIKUNJA_URL` (the public Vikunja domain), `VIKUNJA_TOKEN` (a scoped token: `projects` create/read_all, `tasks` create/read_all/update/delete, `labels` create/read_all, `tasks_labels` update_bulk, `tasks_relations` create/delete, `projects_webhooks` create/read_all), `VIKUNJA_WEBHOOK_SECRET` (`openssl rand -hex 32`). A public domain on port 8080 (`PORT` is set by Railway). One replica: the worker runs in the web process. Postgres backups are the Railway volume's; `todo.py export` is a backup by hand.

**The settings of todo-api** (builder `DOCKERFILE`, health check `/healthz` with 60 s, restart `ON_FAILURE`, one replica in `sfo`) are Railway Infrastructure as Code: `.railway/railway.ts` here, the partial `todo`, which owns only todo-api, so `apply` never touches Vikunja, Postgres or a game's web service; variables stay unmanaged (`railway variables`). A change: `cd tools/todo && npm install && railway config plan`, then `railway config apply` (a linked project, `railway link`).

## Tests

```bash
uv pip install --python .venv/bin/python -r tools/todo/requirements-dev.txt   # once
.venv/bin/python -m pytest tools/todo/tests
```

The service tests need a real Postgres: `TODO_TEST_DATABASE_URL` (wiped), else a `postgres:17-alpine` container is started through docker for the session; without either they skip. `fake_vikunja.py` stands in for the Vikunja 2.7.1 behaviour the mirror relies on.
