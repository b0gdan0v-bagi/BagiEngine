#!/usr/bin/env python3
"""
todo: the command line of the todo store (the service in service/, hosted on Railway).

The store is the source of truth for the entries, their status, the stages, the plans and the
reviews; nothing of it lives in git. The service mirrors every entry into Vikunja by itself (and
takes back what a person changes there), so there is no push and no pull.

Settings: TODO_API_URL and TODO_API_TOKEN from the environment, then from --env-file, then from the
first `.env` found from the current folder upwards. The token is never printed.

Every write that the store accepts also rewrites a small signal file, TODO_CHANGED_FILE (default
~/.claude/todo/changed.json: the time, the actor, the method and the path), so a tool that shows the
store (tools/vscode/todo-workers) reads it again at once instead of on a timer.

Plans and exports are written only OUTSIDE a git work tree (default: ~/.claude/plans/), so a plan
cannot end up in a commit.

Python 3.10+, standard library only.
"""

import argparse
import datetime
import json
import os
import re
import socket
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from todo_markdown import README_FILE, ROADMAP_FILE, ParseError, parse_entry_text, parse_stages  # noqa: E402

ENV_URL = "TODO_API_URL"
ENV_TOKEN = "TODO_API_TOKEN"
ENV_ACTOR = "TODO_ACTOR"
ENV_CHANGED_FILE = "TODO_CHANGED_FILE"
DEFAULT_CHANGED_FILE = Path.home() / ".claude" / "todo" / "changed.json"
DEFAULT_PLANS_DIR = Path.home() / ".claude" / "plans"
REQUEST_TIMEOUT_SECONDS = 30
_ENV_LINE_RE = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
LOOP_PLAN_PREFIX = "todo-loop-"
REVIEW_INFIX = "-review-"
WORKERS_SUFFIX = ".workers"  # the parent folder of the worker folders of worker.py: `<repository>.workers/w1`


class TodoError(Exception):
    """A message for the person at the terminal; the exit code is 1 (2 for a refusal of the store)."""

    def __init__(self, message: str, code: int = 1):
        super().__init__(message)
        self.code = code


# --- settings ----------------------------------------------------------------------------------


def read_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _ENV_LINE_RE.match(line)
        if not match:
            continue
        value = match.group(2)
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[match.group(1)] = value
    return values


def find_env_file(start: Path) -> Path | None:
    for folder in [start, *start.parents]:
        candidate = folder / ".env"
        if candidate.is_file() and ENV_URL in read_env_file(candidate):
            return candidate
    return None


def load_settings(env_file: Path | None) -> tuple[str, str]:
    values = read_env_file(env_file) if env_file else {}
    if not env_file:
        found = find_env_file(Path.cwd())
        values = read_env_file(found) if found else {}
    url = (os.environ.get(ENV_URL) or values.get(ENV_URL) or "").strip().rstrip("/")
    token = (os.environ.get(ENV_TOKEN) or values.get(ENV_TOKEN) or "").strip()
    missing = [name for name, value in ((ENV_URL, url), (ENV_TOKEN, token)) if not value]
    if missing:
        raise TodoError(f"{', '.join(missing)} not set: put them into the environment or the .env of the repository")
    return url, token


def changed_file() -> Path:
    return Path(os.environ.get(ENV_CHANGED_FILE) or DEFAULT_CHANGED_FILE).expanduser()


def signal_change(actor: str, method: str, path: str) -> None:
    """Rewrite the signal file after a write the store accepted (a temporary file, then a rename, so a
    watcher never reads half of it). A signal that cannot be written never fails the command."""
    target = changed_file()
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(f".{target.name}.{os.getpid()}")
        stamp = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")
        temporary.write_text(json.dumps({"at": stamp, "actor": actor, "method": method, "path": path}) + "\n", encoding="utf-8")
        os.replace(temporary, target)
    except OSError:
        pass


class Api:
    def __init__(self, url: str, token: str, actor: str):
        self.url = url
        self._token = token
        self.actor = actor

    def call(self, method: str, path: str, body: dict | None = None, query: dict | None = None):
        address = f"{self.url}/api/v1{path}"
        if query:
            address += "?" + urllib.parse.urlencode({key: value for key, value in query.items() if value is not None})
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(address, data=data, method=method)
        request.add_header("Authorization", f"Bearer {self._token}")
        request.add_header("X-Todo-Actor", self.actor)
        request.add_header("Accept", "application/json")
        if data is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
                payload = response.read()
            if method != "GET":
                signal_change(self.actor, method, path)
            return json.loads(payload) if payload else None
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            try:
                detail = json.loads(detail).get("detail", detail)
            except (json.JSONDecodeError, AttributeError):
                pass
            raise TodoError(f"{method} {path}: HTTP {error.code}: {detail}", 2 if error.code in (404, 409, 422) else 1) from None
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise TodoError(f"the todo service at {self.url} does not answer: {error}") from None

    def get(self, path: str, **query):
        return self.call("GET", path, query=query or None)


# --- files outside git -------------------------------------------------------------------------


def git_work_tree(path: Path) -> Path | None:
    """The top of the git work tree that holds `path` (or its nearest existing parent), or None."""
    folder = path if path.is_dir() else path.parent
    while not folder.exists() and folder != folder.parent:
        folder = folder.parent
    try:
        result = subprocess.run(["git", "-C", str(folder), "rev-parse", "--show-toplevel"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return Path(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else None


def default_origin() -> str:
    """Where this checkout is: `<game repository>@<host>`, the repository name taken from a worker folder (`<name>.workers/w1`) or the git top; the host alone outside git."""
    host = socket.gethostname()
    tree = git_work_tree(Path.cwd())
    if tree is None:
        return host
    name = tree.parent.name.removesuffix(WORKERS_SUFFIX) if tree.parent.name.endswith(WORKERS_SUFFIX) else tree.name
    return f"{name}@{host}"


def outside_git(path: Path, what: str) -> Path:
    path = path.expanduser().resolve()
    tree = git_work_tree(path)
    if tree is not None:
        raise TodoError(f"{what} {path} is inside the git work tree {tree}: plans and exports never go into a repository")
    return path


def read_text(source: str) -> str:
    return sys.stdin.read() if source == "-" else Path(source).expanduser().read_text(encoding="utf-8")


# --- output ------------------------------------------------------------------------------------


def progress_text(entry: dict) -> str:
    progress = entry.get("planProgress")
    if not entry.get("planShown"):
        return f"plan {entry['plan']} (not in the store)" if entry.get("plan") else ""
    count = f" {progress['done']}/{progress['total']}" if progress else " no checklist"
    return f"plan {entry['planShown']}{count}"


def print_entry(entry: dict, out=None) -> None:
    out = out or sys.stdout
    facts = [f"id: {entry['id']}", f"area: {entry['area']}", f"status: {entry['statusLine']}"]
    if entry["stage"] is not None:
        facts.append(f"stage: {entry['stage']}")
    if progress_text(entry):
        facts.append(progress_text(entry))
    where = [f"{name}: {entry[name]}" for name in ("base", "origin") if entry.get(name)]
    if where:
        facts.append(" · ".join(where))
    facts.append(f"version: {entry['version']}")
    links = [
        f"{word}: {', '.join(link_text(link) for link in entry.get(key) or [])}"
        for key, word in (("blockedBy", "blocked by"), ("blocks", "blocks"))
        if entry.get(key)
    ]
    head = "\n".join([" · ".join(facts), *links])
    out.write(f"# {entry['title']}\n\n{head}\n\n{entry['body'].rstrip()}\n")


def link_text(link: dict) -> str:
    return f"{link['id']} ({link['status']}{': ' + link['note'] if link.get('note') else ''})"


def open_blockers(entry: dict) -> list[str]:
    return [link["id"] for link in entry.get("blockedBy") or [] if link["status"] != "closed"]


def print_table(entries: list[dict], out=None) -> None:
    out = out or sys.stdout
    if not entries:
        out.write("no entries\n")
        return
    width = max(len(entry["id"]) for entry in entries)
    for entry in entries:
        stage = f"stage {entry['stage']}" if entry["stage"] is not None else "-"
        line = f"{entry['id']:<{width}}  {entry['area']:<7} {entry['status']:<11} {stage:<8} {entry['title']}"
        extra = [text for text in (progress_text(entry), "blocked by " + ", ".join(open_blockers(entry)) if open_blockers(entry) else "") if text]
        out.write(line + "".join(f"  [{text}]" for text in extra) + "\n")


def emit(value, as_json: bool, printer=None) -> None:
    if as_json or printer is None:
        print(json.dumps(value, ensure_ascii=False, indent=2))
    else:
        printer(value)


# --- commands ----------------------------------------------------------------------------------


def cmd_ping(api: Api, args) -> None:
    health = json.loads(urllib.request.urlopen(f"{api.url}/healthz", timeout=REQUEST_TIMEOUT_SECONDS).read())
    areas = api.get("/areas")
    print(f"ok: {api.url}, areas {', '.join(area['id'] for area in areas) or 'none'}, mirror {'on' if health['mirror'] else 'off'}, "
          f"outbox {health['outbox']} ({health['outboxFailing']} failing)" + (f", mirror error: {health['mirrorError']}" if health["mirrorError"] else "")
          + (f", relations error: {health['relationsError']}" if health.get("relationsError") else ""))


def cmd_list(api: Api, args) -> None:
    statuses = args.status or ("open,in progress,review,deferred" if not args.all else None)
    blocked = "true" if args.blocked else "false" if args.ready else None
    entries = api.get("/entries", area=args.area, status=statuses, body="true" if args.body else None, blocked=blocked)
    emit(entries, args.json, print_table)


def cmd_show(api: Api, args) -> None:
    emit(api.get(f"/entries/{args.id}"), args.json, print_entry)


def cmd_history(api: Api, args) -> None:
    events = api.get(f"/entries/{args.id}/events")
    if args.json:
        emit(events, True)
        return
    for event in events:
        print(f"{event['at'][:19]}  {event['actor']:<12} {event['kind']:<8} {json.dumps(event['data'], ensure_ascii=False)[:160]}")


def cmd_add(api: Api, args) -> None:
    body = read_text(args.body_file) if args.body_file else ""
    entry = api.call(
        "POST", "/entries", {"id": args.id, "area": args.area, "title": args.title, "body": body, "stage": args.stage, "status": args.status}
    )
    emit(entry, args.json, lambda e: print(f"added {e['id']} ({e['area']}): {e['title']}"))


def _optional_int(text: str | None):
    if text is None:
        return None
    return "none" if text == "none" else int(text)


def cmd_edit(api: Api, args) -> None:
    patch: dict = {"clear": []}
    if args.title:
        patch["title"] = args.title
    if args.body_file:
        patch["body"] = read_text(args.body_file)
    if args.status:
        patch["status"] = args.status
    if args.area:
        patch["area"] = args.area
    for name, value in (("stage", _optional_int(args.stage)), ("plan", args.plan)):
        if value == "none":
            patch["clear"].append(name)
        elif value is not None:
            patch[name] = value
    if args.expected_version is not None:
        patch["expectedVersion"] = args.expected_version
    entry = api.call("PATCH", f"/entries/{args.id}", patch)
    emit(entry, args.json, lambda e: print(f"{e['id']}: version {e['version']}, {e['statusLine']}"))


def cmd_claim(api: Api, args) -> None:
    body = {"branch": args.branch}
    if args.base:
        body["base"] = args.base
        body["origin"] = args.origin or default_origin()
    elif args.origin:
        body["origin"] = args.origin
    entry = api.call("POST", f"/entries/{args.id}/claim", body)
    emit(entry, args.json, lambda e: print(f"{e['id']}: {e['statusLine']}"))


def cmd_review(api: Api, args) -> None:
    entry = api.call("POST", f"/entries/{args.id}/review", {"branch": args.branch, "note": args.note})
    emit(entry, args.json, lambda e: print(f"{e['id']}: {e['statusLine']}"))


def cmd_stop(api: Api, args) -> None:
    entry = api.call("POST", f"/entries/{args.id}/stop", {"reason": args.reason})
    emit(entry, args.json, lambda e: print(f"{e['id']}: {e['statusLine']}"))


def cmd_close(api: Api, args) -> None:
    entry = api.call("POST", f"/entries/{args.id}/close", {"reopen": args.reopen})
    emit(entry, args.json, lambda e: print(f"{e['id']}: {e['statusLine']}"))


def cmd_dep(api: Api, args) -> None:
    if args.dep_command == "add":
        entry = None
        for blocker in args.blockers:
            entry = api.call("PUT", f"/entries/{args.id}/blockers/{blocker}", {"note": args.note or ""})
        emit(entry, args.json, lambda e: print(f"{e['id']}: blocked by {', '.join(link_text(link) for link in e['blockedBy'])}"))
        return
    if args.dep_command == "rm":
        entry = None
        for blocker in args.blockers:
            entry = api.call("DELETE", f"/entries/{args.id}/blockers/{blocker}")
        emit(entry, args.json, lambda e: print(f"{e['id']}: blocked by {', '.join(link_text(link) for link in e['blockedBy']) or 'nothing'}"))
        return
    if args.dep_command == "graph":
        query = f"?area={urllib.parse.quote(args.area)}" if args.area else ""
        print(f"{api.url}/dashboard/graph{query}  (log in with any user name and the API token as the password)")
        return
    if args.id:  # list for one entry
        entry = api.get(f"/entries/{args.id}")
        if args.json:
            emit({"id": entry["id"], "blocked": entry["blocked"], "blockedBy": entry["blockedBy"], "blocks": entry["blocks"]}, True)
            return
        print(f"{entry['id']} ({entry['status']}){': blocked' if entry['blocked'] else ''}")
        for key, word in (("blockedBy", "blocked by"), ("blocks", "blocks")):
            for link in entry[key]:
                print(f"  {word:<10} {link_text(link)}  {link['title']}")
        return
    links = api.get("/dependencies")
    if args.json:
        emit(links, True)
        return
    if not links:
        print("no links")
        return
    width = max(len(link["entry"]) for link in links)
    for link in links:
        print(f"{link['entry']:<{width}}  blocked by  {link['blocker']}" + (f"  ({link['note']})" if link["note"] else ""))


def cmd_plan(api: Api, args) -> None:
    if args.plan_command == "list":
        plans = api.get("/plans", entry=args.entry, kind=args.kind)
        if args.json:
            emit(plans, True)
            return
        for plan in plans:
            progress = f"{plan['progress']['done']}/{plan['progress']['total']}" if plan["progress"] else "-"
            print(f"{plan['name']}  {plan['kind']:<6} {plan['entry'] or '-':<30} {progress:<8} {plan['updatedAt'][:16]}")
        return
    if args.plan_command == "get":
        plan = api.get(f"/plans/{args.name}")
        if args.stdout:
            sys.stdout.write(plan["content"])
            return
        target = outside_git(Path(args.out) if args.out else DEFAULT_PLANS_DIR / args.name, "the plan file")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(plan["content"], encoding="utf-8")
        print(target)
        return
    if args.plan_command == "put":
        if args.file == "-":
            content = sys.stdin.read()
        else:
            content = outside_git(Path(args.file) if args.file else DEFAULT_PLANS_DIR / args.name, "the plan file").read_text(encoding="utf-8")
        plan = api.call("PUT", f"/plans/{args.name}", {"content": content, "entry": args.entry, "kind": args.kind})
        progress = f", {plan['progress']['done']}/{plan['progress']['total']}" if plan["progress"] else ""
        emit(plan, args.json, lambda p: print(f"{p['name']}: {p['kind']}, version {p['version']}{progress}"))
        return
    if args.plan_command == "url":
        print(f"{api.url}/dashboard/plan/{urllib.parse.quote(args.name)}")
        return
    if args.plan_command == "delete":
        api.call("DELETE", f"/plans/{args.name}")
        print(f"deleted {args.name}")


def cmd_area(api: Api, args) -> None:
    if args.area_command == "list":
        emit(api.get("/areas"), args.json, lambda areas: [print(f"{a['id']}: {a['title']}, Vikunja {a['vikunja_project'] or '-'}, "
                                                               f"{len(a['stages'])} stages") for a in areas])
        return
    stages = parse_stages(Path(args.roadmap).read_text(encoding="utf-8")) if args.roadmap else None
    area = api.call("PUT", f"/areas/{args.id}", {"title": args.title, "vikunjaProject": args.vikunja_project, "stages": stages})
    print(f"{area['id']}: {area['title']}, Vikunja {area['vikunja_project'] or '-'}, {len(area['stages'])} stages")


def cmd_dashboard(api: Api, args) -> None:
    query = f"?area={urllib.parse.quote(args.area)}" if args.area else ""
    print(f"{api.url}/dashboard/{query}  (log in with any user name and the API token as the password)")


def cmd_admin(api: Api, args) -> None:
    if args.admin_command == "reconcile":
        emit(api.call("POST", "/admin/vikunja/reconcile"), True)
    elif args.admin_command == "webhooks":
        for line in api.call("POST", "/admin/vikunja/webhooks")["lines"]:
            print(line)
    elif args.admin_command == "dedupe":
        for line in api.call("POST", "/admin/vikunja/dedupe", query={"apply": "true" if args.apply else "false"})["lines"]:
            print(line)
    elif args.admin_command == "outbox":
        emit(api.get("/admin/outbox"), True)


def loop_plan_entry(name: str, entry_ids: set[str]) -> tuple[str, str] | None:
    """
    `todo-loop-<branch-id>.md` (a slice plan) or `todo-loop-<branch-id>-review-<x>.md` (a review)
    -> (entry id, kind); the entry id is `<branch-id>` or `<branch-id>` minus a `-K` slice suffix.
    """
    if not (name.startswith(LOOP_PLAN_PREFIX) and name.endswith(".md")):
        return None
    stem = name[len(LOOP_PLAN_PREFIX) : -len(".md")]
    candidates = [(stem, "slice")]
    cut = len(stem)
    while (cut := stem.rfind(REVIEW_INFIX, 0, cut)) > 0:
        candidates.append((stem[:cut], "review"))
    for branch_id, kind in candidates:
        for entry_id in (branch_id, re.sub(r"-\d+$", "", branch_id)):
            if entry_id in entry_ids:
                return entry_id, kind
    return None


def cmd_import(api: Api, args) -> None:
    """The old entry files of one area (and their plans) into the store. Existing entries are skipped."""
    todo_dir = Path(args.todo)
    plans_dir = Path(args.plans_dir).expanduser()
    existing = {entry["id"] for entry in api.get("/entries", status="open,in progress,review,deferred,closed")}
    records = []
    for path in sorted(todo_dir.glob("*.md")):
        if path.name in (README_FILE, ROADMAP_FILE):
            continue
        try:
            record = parse_entry_text(path.read_text(encoding="utf-8"), path.name)
        except ParseError as error:
            raise TodoError(str(error)) from None
        # the file name is the id; an old `id:` kept for the tracker becomes an alias (its Vikunja marker still links)
        record["aliases"] = [record["id"]] if record["id"] != path.stem else []
        record["id"] = path.stem
        records.append(record)
    ids = {record["id"] for record in records}
    alias_of = {alias: record["id"] for record in records for alias in record["aliases"]}
    for record in records:
        if record["id"] in existing:
            print(f"skip    {record['id']} (in the store)")
            continue
        body = {
            "id": record["id"],
            "area": args.area,
            "title": record["title"],
            "body": record["body"],
            "status": record["status"],
            "statusDate": record["statusDate"],
            "statusNote": record["statusNote"],
            "branch": record["branch"],
            "stage": record["stage"],
            "plan": record["plan"],
            "aliases": record["aliases"],
        }
        alias = f", was {record['aliases'][0]}" if record["aliases"] else ""
        print(f"{'would add' if args.dry_run else 'add':<7} {record['id']} ({record['status']}{alias})")
        if not args.dry_run:
            api.call("POST", "/entries", body)
    plans: list[tuple[str, str | None, str]] = []
    for name in sorted({record["plan"] for record in records if record["plan"]}):
        plans.append((name, None, "owner"))
    if plans_dir.is_dir():
        for path in sorted(plans_dir.glob("todo-loop-*.md")):
            found = loop_plan_entry(path.name, ids | set(alias_of))
            if found:
                plans.append((path.name, alias_of.get(found[0], found[0]), found[1]))
    for name, entry_id, kind in plans:
        source = plans_dir / name
        if not source.is_file():
            print(f"missing plan {name} (not in {plans_dir})")
            continue
        print(f"{'would put' if args.dry_run else 'put':<7} plan {name} ({kind}{', ' + entry_id if entry_id else ''})")
        if not args.dry_run:
            api.call("PUT", f"/plans/{name}", {"content": source.read_text(encoding="utf-8"), "entry": entry_id, "kind": kind})


def cmd_export(api: Api, args) -> None:
    """Every entry (in the old file format) and every plan into a folder outside git: a backup to read offline."""
    target = outside_git(Path(args.out), "the export folder")
    entries = api.get("/entries", area=args.area, status="open,in progress,review,deferred,closed", body="true")
    for entry in entries:
        folder = target / entry["area"] / ("closed" if entry["status"] == "closed" else "")
        folder.mkdir(parents=True, exist_ok=True)
        front = [f"status: {entry['statusLine']}"] if entry["status"] != "open" else []
        if entry["stage"] is not None:
            front.append(f"stage: {entry['stage']}")
        if entry["plan"]:
            front.append(f"plan: {entry['plan']}")
        head = "---\n" + "\n".join(front) + "\n---\n" if front else ""
        (folder / f"{entry['id']}.md").write_text(f"{head}# {entry['title']}\n\n{entry['body'].rstrip()}\n", encoding="utf-8")
    plans = api.get("/plans")
    (target / "plans").mkdir(parents=True, exist_ok=True)
    for plan in plans:
        content = api.get(f"/plans/{plan['name']}")["content"]
        (target / "plans" / plan["name"]).write_text(content, encoding="utf-8")
    print(f"{len(entries)} entries and {len(plans)} plans in {target}")


# --- arguments ---------------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="todo.py", description="The todo store (source of truth; mirrored into Vikunja by the service).")
    parser.add_argument("--env-file", type=Path, help="the .env with TODO_API_URL and TODO_API_TOKEN")
    parser.add_argument("--actor", default=None, help="who writes, for the history (default $TODO_ACTOR or 'cli')")
    parser.add_argument("--json", action="store_true", help="print JSON")
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="print JSON")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", parents=[common], help="check the service, the token and the mirror")

    p = sub.add_parser("list", parents=[common], help="entries (open, in progress, review, deferred unless --status / --all)")
    p.add_argument("--area")
    p.add_argument("--status", help="comma separated: open,in progress,review,deferred,closed")
    p.add_argument("--all", action="store_true", help="closed ones too")
    p.add_argument("--body", action="store_true", help="with the body (JSON)")
    group = p.add_mutually_exclusive_group()
    group.add_argument("--ready", action="store_true", help="only entries whose blockers are all closed")
    group.add_argument("--blocked", action="store_true", help="only entries with a blocker that is not closed")

    p = sub.add_parser("show", parents=[common], help="one entry as markdown")
    p.add_argument("id")

    p = sub.add_parser("history", parents=[common], help="the events of one entry, newest first")
    p.add_argument("id")

    p = sub.add_parser("add", parents=[common], help="a new entry")
    p.add_argument("id")
    p.add_argument("--area", required=True)
    p.add_argument("--title", required=True)
    p.add_argument("--stage", type=int)
    p.add_argument("--status", default="open", choices=["open", "deferred"])
    p.add_argument("--body-file", help="markdown file of the body, '-' for stdin")

    p = sub.add_parser("edit", parents=[common], help="change fields of an entry")
    p.add_argument("id")
    p.add_argument("--title")
    p.add_argument("--body-file", help="markdown file of the new body, '-' for stdin")
    p.add_argument("--stage", help="a number or 'none'")
    p.add_argument("--plan", help="the owner's plan name or 'none'")
    p.add_argument("--status", choices=["open", "deferred", "in progress", "closed"])
    p.add_argument("--area")
    p.add_argument("--expected-version", type=int)

    p = sub.add_parser("claim", parents=[common], help="the loop takes an open entry (open -> in progress)")
    p.add_argument("id")
    p.add_argument("--branch", required=True)
    p.add_argument("--base", help="the base branch the entry's branch is cut from (stored with the claim)")
    p.add_argument("--origin", help="the checkout that made the branch, `<game repository>@<host>` (default: this one, when --base is given)")

    p = sub.add_parser("review", parents=[common], help="the slice waits for the owner (in progress -> review)")
    p.add_argument("id")
    p.add_argument("--branch", required=True)
    p.add_argument("--note", required=True)

    p = sub.add_parser("stop", parents=[common], help="the task stopped (stays in progress)")
    p.add_argument("id")
    p.add_argument("--reason", required=True)

    p = sub.add_parser("close", parents=[common], help="after the merge: closed, or open again for the next slice (--reopen)")
    p.add_argument("id")
    p.add_argument("--reopen", action="store_true")

    p = sub.add_parser("dep", parents=[common], help="dependencies: what an entry waits for (is blocked by) and what it blocks")
    dep_sub = p.add_subparsers(dest="dep_command", required=True)
    q = dep_sub.add_parser("add", parents=[common], help="<id> is blocked by each <blocker> (until it is closed)")
    q.add_argument("id")
    q.add_argument("blockers", nargs="+")
    q.add_argument("--note", help="why, in a few words")
    q = dep_sub.add_parser("rm", parents=[common], help="<id> no longer waits for each <blocker>")
    q.add_argument("id")
    q.add_argument("blockers", nargs="+")
    q = dep_sub.add_parser("list", parents=[common], help="every link, or the links of one entry")
    q.add_argument("id", nargs="?")
    q = dep_sub.add_parser("graph", parents=[common], help="print the address of the graph page")
    q.add_argument("--area")

    p = sub.add_parser("plan", parents=[common], help="plans and reviews")
    plan_sub = p.add_subparsers(dest="plan_command", required=True)
    q = plan_sub.add_parser("list", parents=[common])
    q.add_argument("--entry")
    q.add_argument("--kind", choices=["owner", "slice", "review"])
    q = plan_sub.add_parser("get", parents=[common], help=f"write the plan to a file outside git (default {DEFAULT_PLANS_DIR}/NAME)")
    q.add_argument("name")
    q.add_argument("--out")
    q.add_argument("--stdout", action="store_true")
    q = plan_sub.add_parser("put", parents=[common], help=f"store a plan from a file outside git (default {DEFAULT_PLANS_DIR}/NAME)")
    q.add_argument("name")
    q.add_argument("--file", help="the plan file, '-' for stdin")
    q.add_argument("--entry")
    q.add_argument("--kind", choices=["owner", "slice", "review"])
    q = plan_sub.add_parser("url", parents=[common], help="the page of the plan in the browser")
    q.add_argument("name")
    q = plan_sub.add_parser("delete", parents=[common])
    q.add_argument("name")

    p = sub.add_parser("area", parents=[common], help="the areas (one per todo list and Vikunja project)")
    area_sub = p.add_subparsers(dest="area_command", required=True)
    area_sub.add_parser("list", parents=[common])
    q = area_sub.add_parser("set", parents=[common])
    q.add_argument("id")
    q.add_argument("--title", required=True)
    q.add_argument("--vikunja-project")
    q.add_argument("--roadmap", help="a roadmap.md whose numbered list gives the stages")

    p = sub.add_parser("dashboard", parents=[common], help="print the address of the dashboard")
    p.add_argument("--area")

    p = sub.add_parser("admin", parents=[common], help="the mirror")
    admin_sub = p.add_subparsers(dest="admin_command", required=True)
    admin_sub.add_parser("reconcile", parents=[common], help="compare every Vikunja project with the store now")
    admin_sub.add_parser("webhooks", parents=[common], help="install the Vikunja webhooks")
    admin_sub.add_parser("outbox", parents=[common], help="the tasks waiting to be written")
    q = admin_sub.add_parser("dedupe", parents=[common], help="entries with several open tasks: keep the oldest, delete the rest")
    q.add_argument("--apply", action="store_true", help="do it (without: only the report)")

    p = sub.add_parser("import", parents=[common], help="old entry files of one area (and their plans) into the store")
    p.add_argument("--area", required=True)
    p.add_argument("--todo", required=True, help="the old todo folder")
    p.add_argument("--plans-dir", default=str(DEFAULT_PLANS_DIR))
    p.add_argument("--dry-run", action="store_true")

    p = sub.add_parser("export", parents=[common], help="every entry and plan into a folder outside git")
    p.add_argument("--out", required=True)
    p.add_argument("--area")
    return parser


COMMANDS = {
    "ping": cmd_ping,
    "list": cmd_list,
    "show": cmd_show,
    "history": cmd_history,
    "add": cmd_add,
    "edit": cmd_edit,
    "claim": cmd_claim,
    "review": cmd_review,
    "stop": cmd_stop,
    "close": cmd_close,
    "dep": cmd_dep,
    "plan": cmd_plan,
    "area": cmd_area,
    "dashboard": cmd_dashboard,
    "admin": cmd_admin,
    "import": cmd_import,
    "export": cmd_export,
}


def main(argv: list[str] | None = None) -> int:
    # A pipe on Windows gets the ANSI code page (cp1251), which has no "→" and garbles what it has: the text is always UTF-8
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    args = build_parser().parse_args(argv)
    try:
        url, token = load_settings(args.env_file)
        api = Api(url, token, args.actor or os.environ.get(ENV_ACTOR) or "cli")
        COMMANDS[args.command](api, args)
    except TodoError as error:
        print(f"todo: {error}", file=sys.stderr)
        return error.code
    except (OSError, ValueError) as error:
        print(f"todo: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
