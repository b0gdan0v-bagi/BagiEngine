"""
worker_insights: the views of the VS Code panel (tools/vscode/todo-workers) over the workers and the todo store that
are slower than `worker.py list` and are read less often: the busy files (what keeps an open entry from a worker), the
merge train (every ready review branch merged one by one), the day (what was merged, what waits, what is not pushed),
the timeline (each worker's sessions over the last hours) and the review rounds of each entry.

`worker.py <command>` runs them (`busy`, `merge-train`, `day`, `timeline`, `review-stats`); they read, except the
merge train, which merges as `worker.py merge` does. Nothing here pushes or fetches: "not pushed" compares the local
branches with the remote refs the last fetch left.
"""

import json
import os
import re
import subprocess
import time
from datetime import datetime, timedelta
from pathlib import Path

import worker as w

# --- busy files: which taken entry holds which files, and which open entries meet them ----------------------------

FOLDER_DEPTH = 3  # a file counts under its first three path parts (`src/Modules/BECore/`)
SHARED_FILES = {"CLAUDE.md", "README.md"}  # every entry touches them a little (todo-project.md, "Workers")


def folder_of(path: str) -> str:
    parts = path.strip("/").split("/")
    return "/".join(parts[: min(FOLDER_DEPTH, len(parts) - 1)]) + "/" if len(parts) > 1 else path


def folders(files: list[str]) -> list[dict]:
    counts: dict[str, int] = {}
    for path in files:
        counts[folder_of(path)] = counts.get(folder_of(path), 0) + 1
    return [{"path": p, "count": n} for p, n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))]


LOOKS_LIKE_PATH = re.compile(r"[A-Za-z0-9_.\-/]+")


def area_paths(paths, area: dict, game: bool) -> list[str]:
    """Paths relative to the area's repository: in a game an engine entry's plan names `engine/src/...` and a game entry
    may name engine files (another repository, never a conflict with a game entry). Words that are no path go."""
    prefix = f"{w.SHARED_SUBMODULE}/"
    shown = set()
    for path in paths:
        if not LOOKS_LIKE_PATH.fullmatch(path) or ("/" not in path and "." not in path):
            continue
        if game and area["repo"] == w.SHARED_SUBMODULE:
            path = path[len(prefix):] if path.startswith(prefix) else path
        elif game and path.startswith(prefix):
            continue
        if path not in SHARED_FILES:
            shown.add(path)
    return sorted(shown)


def busy_map(layout: w.Layout) -> dict:
    """`taken`: the entries in progress or in review with a branch, the worker that holds it and their busy files
    (todo-loop, "Busy files"); `open`: each open entry of this checkout's areas with the files it is known to touch
    (the classifier's cached `touches`, else the paths its text names) and the taken entries those files meet."""
    table = {row["area"]: row for row in w.rule_area_table(layout.main)}
    entries = json.loads(w.todo_cli(layout, "list", "--status", "open,in progress,review", "--json", "--body") or "[]")
    entries = [e for e in entries if isinstance(e, dict) and (not table or e.get("area") in table)]
    held = {}
    for k, path in layout.workers():
        for repo in (path, path / w.SHARED_SUBMODULE):
            if (repo / ".git").exists():
                branch = w.head_json(repo)["branch"]
                if branch:
                    held[branch] = f"{w.WORKER_PREFIX}{k}"
    taken = []
    for e in entries:
        if e.get("status") in ("in progress", "review") and e.get("branch") and e.get("area") in table:
            area = table[e["area"]]
            repo = (layout.main / area["repo"]).resolve()
            if not w.git_ok("rev-parse", "--verify", "--quiet", f"refs/heads/{e['branch']}", cwd=repo):
                continue  # taken in another checkout: only its plan would say, and the loop there owns it
            files = area_paths(w.busy_files(layout, e, area), area, layout.shared is not None)
            taken.append({"id": e["id"], "area": e["area"], "title": e.get("title"), "status": e["status"],
                          "worker": held.get(e["branch"]), "files": files, "folders": folders(files)})
    cache = w.read_rooted(w.verdicts_file(), layout)
    now = time.time()
    shown = []
    for e in entries:
        if e.get("status") != "open":
            continue
        verdict = w.fresh_verdict(cache.get(e["id"]), e.get("version"), now)
        touches = (verdict or {}).get("touches") or []
        source = "verdict" if touches else None
        if not touches:
            touches = sorted(w.paths_in(f"{e.get('title') or ''}\n{e.get('body') or ''}"))
            source = "text" if touches else None
        touches = area_paths(touches, table[e["area"]], layout.shared is not None) if e.get("area") in table else touches
        source = source if touches else None
        meets = []
        for t in taken:
            if t["area"] != e["area"]:
                continue  # another repository: never a conflict
            common = w.overlapping(set(touches), set(t["files"]))
            if common:
                meets.append({"id": t["id"], "worker": t["worker"], "files": common[:8], "count": len(common)})
        shown.append({"id": e["id"], "area": e["area"], "title": e.get("title"), "priority": e.get("priority"),
                      "blocked": bool(e.get("blocked")), "touches": touches[:20], "source": source, "meets": meets})
    shown.sort(key=lambda o: (o["blocked"], bool(o["meets"]), o["source"] is None, w.PRIORITY_RANK.get(o["priority"], w.PRIORITY_RANK[None]), o["id"]))
    return {"taken": taken, "open": shown}


def cmd_busy(layout: w.Layout, args) -> None:
    result = busy_map(layout)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
        return
    for t in result["taken"]:
        where = f" ({t['worker']})" if t["worker"] else ""
        print(f"{t['id']}{where}: {', '.join(f['path'] + ' ' + str(f['count']) for f in t['folders'][:6]) or '-'}")
    for o in result["open"]:
        meets = "; ".join(f"{m['id']}: {', '.join(m['files'][:3])}" for m in o["meets"])
        print(f"  {o['id']:40} {'blocked ' if o['blocked'] else ''}{'meets ' + meets if meets else ('free' if o['source'] else 'files unknown')}")


# --- the merge train: every ready review branch, one by one --------------------------------------------------------


def train_order(layout: w.Layout) -> list[dict]:
    """The entries in review this checkout can merge (local branch, its own base), engine ones first (a game entry may
    be paired on one), then those with fewer conflicts with the other review branches, then by id."""
    entries, error = w.store_entries(layout)
    if error:
        raise w.WorkerError(f"todo store: {error}")
    w.review_checks(entries)
    ready = [e for e in entries if e.get("status") == "review" and e.get("local") and not e.get("foreignBase")]
    shared = str(layout.shared.resolve()) if layout.shared is not None else None
    ready.sort(key=lambda e: (e.get("repo") != shared, len(e.get("conflictsWith") or []), bool(e.get("conflicts")), e["id"]))
    return ready


def cmd_merge_train(layout: w.Layout, args) -> None:
    """`merge-train [--dry-run]`: merge every ready entry in review as `merge` does, in `train_order`. Each merge is
    planned again after the one before, so a branch the moved base now conflicts with is caught; an entry that waits
    for another of the train goes after it. The train stops at the first conflict or refusal and names it; the merges
    before it stay. One `kick` at the end. It never builds: the panel runs Verify once after it."""
    order = train_order(layout)
    if args.dry_run:
        rows = []
        for e in order:
            try:
                plan = w.merge_plan(layout, e["id"])
                rows.append({"id": e["id"], "title": e.get("title"), "branch": plan["branch"], "base": plan["base"], "commits": len(plan["commits"]),
                             "conflicts": plan["conflicts"], "problems": plan["problems"], "first": plan["first"], "reopen": plan["reopen"],
                             "pointer": plan["pointer"], "conflictsWith": [c["id"] for c in e.get("conflictsWith") or []]})
            except w.WorkerError as error:
                rows.append({"id": e["id"], "title": e.get("title"), "refused": str(error)})
        print(json.dumps({"order": rows}, ensure_ascii=False, indent=1) if args.json else "\n".join(
            f"{r['id']}: {r.get('refused') or ('conflicts ' + ', '.join(r['conflicts']) if r['conflicts'] else '; '.join(r['problems']) or str(r['commits']) + ' commits')}" for r in rows))
        return
    pending = [e["id"] for e in order]
    merged, stopped = [], None
    while pending and stopped is None:
        # the first entry that waits for no other of the train; after each merge the scan starts again, so an entry
        # that waited for the one just merged goes next
        entry_id, plan = None, None
        for candidate in pending:
            try:
                plan = w.merge_plan(layout, candidate)
            except w.WorkerError as error:
                stopped = {"id": candidate, "why": str(error)}
                break
            waits_in_train = [b for b in plan["first"] if b in pending]
            if not (waits_in_train and len(waits_in_train) == len(plan["first"])):
                entry_id = candidate
                break
        if stopped:
            break
        if entry_id is None:
            stopped = {"id": pending[0], "why": "waits for an entry that is not in this train"}
            break
        if plan["conflicts"] or plan["problems"]:
            why = f"conflicts with {plan['base']}: {', '.join(plan['conflicts'])}" if plan["conflicts"] else "; ".join(plan["problems"])
            stopped = {"id": entry_id, "why": why}
            break
        holders = w.entry_holders(layout, entry_id, Path(plan["heldBy"]) if plan["heldBy"] else None)
        try:
            result = w.do_merge(layout, plan)
        except w.WorkerError as error:
            stopped = {"id": entry_id, "why": str(error)}
            break
        w.reset_streaks(layout, holders)
        merged.append(result)
        pending.remove(entry_id)
    kicked = None
    if merged and not args.no_kick and layout.workers():
        try:
            kicked = w.kick(layout, args.claude, args.delay)
        except w.WorkerError as error:
            merged[-1].setdefault("notes", []).append(f"kick: {error}")
    out = {"merged": merged, "stopped": stopped, "left": [i for i in pending if not stopped or i != stopped["id"]], "kick": kicked}
    if args.json:
        print(json.dumps(out, ensure_ascii=False, indent=1))
        return
    for r in merged:
        print(f"merged {r['branch']} into {r['base']}: {r['merge']}, the entry is {r['status'] or 'still in review'}" + (f", pointer {r['pointer']}" if r.get("pointer") else ""))
    if stopped:
        print(f"stopped at {stopped['id']}: {stopped['why']}")


# --- the day: what was merged, what waits, what is not pushed -------------------------------------------------------

MERGE_SUBJECT = re.compile(r"^Merge branch '(todo/[^']+)'")


def since_of(text: str | None) -> datetime:
    """`--since` (ISO, or ms since the epoch from the panel), else today 00:00 local time."""
    if text:
        if text.isdigit():
            return datetime.fromtimestamp(int(text) / 1000).astimezone()
        return datetime.fromisoformat(text.replace("Z", "+00:00")).astimezone()
    return datetime.now().astimezone().replace(hour=0, minute=0, second=0, microsecond=0)


def push_state(repo: Path, label: str) -> list[dict]:
    """The base branches (`main`, `games/*`) of `repo` against their upstream as the last fetch left it."""
    rows = []
    listed = w.git("for-each-ref", "--format=%(refname:short)\t%(upstream:short)\t%(upstream:track)", "refs/heads/main", "refs/heads/games/", cwd=repo, check=False)
    for line in listed.splitlines():
        name, upstream, track = (line.split("\t") + ["", ""])[:3]
        if not upstream:
            remote = f"origin/{name}"
            if not w.git_ok("rev-parse", "--verify", "--quiet", f"refs/remotes/{remote}", cwd=repo):
                rows.append({"repo": label, "branch": name, "upstream": None, "ahead": None, "behind": None})
                continue
            upstream = remote
        counts = w.git("rev-list", "--left-right", "--count", f"{upstream}...{name}", cwd=repo, check=False).split()
        behind, ahead = (int(counts[0]), int(counts[1])) if len(counts) == 2 else (0, 0)
        rows.append({"repo": label, "branch": name, "upstream": upstream, "ahead": ahead, "behind": behind, "gone": "gone" in track})
    return rows


def day_state(layout: w.Layout, since: datetime) -> dict:
    stamp = since.isoformat()
    table = w.rule_area_table(layout.main)
    merged = []
    seen = set()
    for area in table:
        repo = (layout.main / area["repo"]).resolve()
        if (str(repo), area["base"]) in seen:
            continue
        seen.add((str(repo), area["base"]))
        log = w.git("log", "--first-parent", f"--since={stamp}", "--format=%h\t%cI\t%s", area["base"], cwd=repo, check=False)
        for line in log.splitlines():
            sha, when, subject = (line.split("\t", 2) + ["", ""])[:3]
            found = MERGE_SUBJECT.match(subject)
            if found:
                merged.append({"area": area["area"], "branch": found.group(1), "commit": sha, "at": when})
    merged.sort(key=lambda m: m["at"])
    pushes = []
    repos = [(layout.main, ".")] + ([(layout.shared, w.SHARED_SUBMODULE)] if layout.shared is not None else [])
    for repo, label in repos:
        pushes += push_state(repo, label)
    # the engine goes first: the game's pointer names an engine commit the remote must have
    commands = [f"git -C {p['repo']} push origin {p['branch']}" for p in sorted(pushes, key=lambda p: p["repo"] == ".") if p["ahead"]]
    entries, error = w.store_entries(layout)
    local = [e for e in entries if e.get("local") and not e.get("foreignBase")]
    return {
        "since": stamp,
        "merged": merged,
        "review": [{"id": e["id"], "title": e.get("title")} for e in local if e.get("status") == "review"],
        "inProgress": [{"id": e["id"], "title": e.get("title"), "note": e.get("statusNote")} for e in local if e.get("status") == "in progress"],
        "queues": w.read_queues(layout),
        "push": pushes,
        "pushCommands": commands,
        "store": error or "ok",
    }


def cmd_day(layout: w.Layout, args) -> None:
    state = day_state(layout, since_of(args.since))
    if args.json:
        print(json.dumps(state, ensure_ascii=False, indent=1))
        return
    print(f"since {state['since']}: merged {len(state['merged'])}")
    for m in state["merged"]:
        print(f"  {m['at'][11:16]} {m['branch']} ({m['commit']})")
    print(f"review: {', '.join(e['id'] for e in state['review']) or '-'}")
    print(f"in progress: {', '.join(e['id'] for e in state['inProgress']) or '-'}")
    for line in state["pushCommands"] or ["nothing to push"]:
        print(line)


# --- the timeline: every session of every worker over the last hours ------------------------------------------------

TAKEN = re.compile(r"\b(claim|review) ([a-z0-9][A-Za-z0-9_-]+) --branch")
THROUGH_NOTE = re.compile(r"\bnote ([a-z0-9][A-Za-z0-9_-]+) --note \\?\"through: slice")
STAMP = re.compile(r'"timestamp":\s*"([^"]+)"')
TIMELINE_READ_BYTES = 16 * 1024 * 1024


def ms_of(stamp: str) -> int | None:
    try:
        return int(datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp() * 1000)
    except ValueError:
        return None


def session_span(path: Path, since_ms: int, running: set[str]) -> dict | None:
    """One session of a worker from its transcript: when it started and last did something, the entry it worked
    (its `/todo-loop <id>` prompt, else its claim) and how it ended: `review`, `through` (a slice approved, the next
    one follows), `limited`, `running`, or `ended` (anything else: nothing taken, stopped, lost)."""
    try:
        if path.stat().st_mtime * 1000 < since_ms:
            return None  # Claude Code touches old transcripts too, but never makes one older
        with path.open(encoding="utf-8", errors="replace") as f:
            text = f.read(TIMELINE_READ_BYTES)
    except OSError:
        return None
    first = STAMP.search(text)
    if not first:
        return None
    end = w.last_step_ms(w.tail_records(path), path)
    start = ms_of(first.group(1))
    if start is None or end < since_ms:
        return None
    head = w.LOOP_PROMPT.search(text[: w.LOOP_PROMPT_BYTES])
    entry = w.entry_of_loop_args(head.group(1)) if head else None
    hits = TAKEN.findall(text)
    if entry is None:
        entry = next((i for verb, i in hits if verb == "claim"), None)
    reviewed = {i for verb, i in hits if verb == "review"}
    through = set(THROUGH_NOTE.findall(text))
    if path.stem in running:
        outcome = "running"
    elif any(r.get("type") == "assistant" and (r.get("error") == w.LIMIT_ERROR or r.get("apiErrorStatus") == w.LIMIT_STATUS) for r in w.tail_records(path)[-3:]):
        outcome = "limited"
    elif entry and entry in reviewed or (not entry and reviewed):
        outcome = "review"
    elif entry and entry in through:
        outcome = "through"
    else:
        outcome = "ended"
    return {"id": path.stem, "start": start, "end": end, "entry": entry or (sorted(reviewed)[0] if reviewed else None), "outcome": outcome,
            "loop": bool(head)}


def span_cache_file() -> Path:
    """The spans of finished sessions by transcript, keyed by its size: a finished transcript does not grow, so the
    panel's timeline reads only the running and the new ones (outside every repository)."""
    return w.claude_home() / "todo" / "timeline-cache.json"


def read_span_cache() -> dict:
    try:
        cache = json.loads(span_cache_file().read_text())
    except (OSError, ValueError):
        return {}
    return cache if isinstance(cache, dict) else {}


def write_span_cache(seen: dict) -> None:
    path = span_cache_file()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(seen))
        temporary.replace(path)
    except OSError:
        pass  # no cache: the next read parses again


def cached_span(cache: dict, seen: dict, transcript: Path, since: int, running: set[str]) -> dict | None:
    """`session_span` of a transcript, from the cache while its size holds and it is not running. Every transcript
    that may be in the window is remembered in `seen` (the next cache), with or without a span."""
    try:
        stat = transcript.stat()
    except OSError:
        return None
    if stat.st_mtime * 1000 < since:
        return None  # older than the window: Claude Code only ever touches a transcript later, never earlier
    key = str(transcript)
    hit = cache.get(key)
    if isinstance(hit, dict) and hit.get("size") == stat.st_size and transcript.stem not in running:
        span = hit.get("span")
    else:
        span = session_span(transcript, 0, running)
    if transcript.stem not in running:  # a running session is parsed again next time: its span still grows
        seen[key] = {"size": stat.st_size, "span": span}
    return span if span and span["end"] >= since else None


def timeline(layout: w.Layout, hours: float, claude: str) -> dict:
    now = int(time.time() * 1000)
    since = now - int(hours * 3600 * 1000)
    sessions = w.list_sessions(claude) or []
    running = {str(s.get("sessionId")) for s in sessions if w.is_running(s) and s.get("sessionId")}
    cache = read_span_cache()
    seen: dict[str, dict] = {}
    lanes = []
    for k, path in layout.workers():
        folder = w.projects_dir() / re.sub(r"[^A-Za-z0-9]", "-", str(path))
        spans = []
        if folder.is_dir():
            for transcript in folder.glob("*.jsonl"):
                span = cached_span(cache, seen, transcript, since, running)
                if span:
                    spans.append(span)
        spans.sort(key=lambda s: s["start"])
        busy = 0
        cursor = since
        for s in spans:
            start, end = max(s["start"], cursor), min(s["end"] if s["outcome"] != "running" else now, now)
            if end > start:
                busy += end - start
                cursor = end
        lanes.append({"k": k, "name": f"{w.WORKER_PREFIX}{k}", "spans": spans, "busyShare": round(busy / max(1, now - since), 3)})
    write_span_cache(seen)
    return {"since": since, "now": now, "hours": hours, "lanes": lanes}


def cmd_timeline(layout: w.Layout, args) -> None:
    result = timeline(layout, args.hours, args.claude)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
        return
    for lane in result["lanes"]:
        print(f"{lane['name']}: busy {round(100 * lane['busyShare'])}%")
        for s in lane["spans"]:
            start = datetime.fromtimestamp(s["start"] / 1000).strftime("%H:%M")
            end = datetime.fromtimestamp(s["end"] / 1000).strftime("%H:%M")
            print(f"  {start}-{end} {s['entry'] or '?':40} {s['outcome']}")


# --- review rounds: how often the reviewer and the owner sent a slice back -----------------------------------------

REVIEW_NAME = re.compile(r"^todo-loop-(.+)-review-(.+?)(?:-r(\d+))?\.md$")


def review_stats(layout: w.Layout, days: float) -> dict:
    """From the names of the review plans (`todo-loop-<plan id>-review-<name>[-r<N>].md`: round N of that review) and
    the remarks plans: per entry the reviews, the rounds the reviewer sent back (CHANGES) and the owner's remarks
    rounds; the totals over the last `days`."""
    plans = json.loads(w.todo_cli(layout, "plan", "list", "--json") or "[]")
    areas = w.rule_areas(layout.main)
    if areas:
        listed = json.loads(w.todo_cli(layout, "list", "--all", "--json") or "[]")
        ours = {e["id"] for e in listed if isinstance(e, dict) and e.get("area") in areas}
        plans = [p for p in plans if p.get("entry") in ours]
    cutoff = datetime.now().astimezone() - timedelta(days=days)
    per: dict[str, dict] = {}
    rounds: dict[tuple, dict] = {}
    for plan in plans:
        entry, name, kind = plan.get("entry"), plan.get("name") or "", plan.get("kind")
        if not entry:
            continue
        row = per.setdefault(entry, {"reviews": 0, "changes": 0, "remarks": 0, "recent": False})
        try:
            if datetime.fromisoformat(str(plan.get("updatedAt")).replace("Z", "+00:00")) >= cutoff:
                row["recent"] = True
        except ValueError:
            pass
        if kind == "remarks":
            row["remarks"] += 1
            continue
        found = REVIEW_NAME.match(name) if kind == "review" else None
        if not found:
            continue
        key = (entry, found.group(1), found.group(2))
        group = rounds.setdefault(key, {"rounds": 1})
        group["rounds"] = max(group["rounds"], int(found.group(3) or 1))
    for (entry, _pid, _name), group in rounds.items():
        per[entry]["reviews"] += 1
        per[entry]["changes"] += group["rounds"] - 1
    recent = {k: v for k, v in per.items() if v["recent"] and (v["reviews"] or v["remarks"])}
    reviews = sum(v["reviews"] for v in recent.values())
    changes = sum(v["changes"] for v in recent.values())
    first_time = sum(1 for (entry, _p, _n), g in rounds.items() if entry in recent and g["rounds"] == 1)
    top = sorted(recent.items(), key=lambda kv: (-(kv[1]["changes"] + kv[1]["remarks"]), kv[0]))[:5]
    return {
        "days": days,
        "entries": {k: {key: v[key] for key in ("reviews", "changes", "remarks")} for k, v in per.items() if v["reviews"] or v["remarks"]},
        "total": {"entries": len(recent), "reviews": reviews, "changes": changes, "remarks": sum(v["remarks"] for v in recent.values()),
                  "firstTime": first_time},
        "most": [{"id": k, "changes": v["changes"], "remarks": v["remarks"]} for k, v in top if v["changes"] or v["remarks"]],
    }


def cmd_review_stats(layout: w.Layout, args) -> None:
    stats = review_stats(layout, args.days)
    if args.json:
        print(json.dumps(stats, ensure_ascii=False, indent=1))
        return
    t = stats["total"]
    print(f"last {stats['days']:g} days: {t['entries']} entries, {t['reviews']} reviews, {t['firstTime']} approved at once, "
          f"{t['changes']} rounds sent back by the reviewer, {t['remarks']} remarks rounds")
    for m in stats["most"]:
        print(f"  {m['id']}: {m['changes']} reviewer rounds, {m['remarks']} remarks")


# --- the machine: its load, its memory, and which checkout and worker runs what ------------------------------------
#
# One `ps` and one `sysctl` (about 30 ms together), so the panel can read it on every refresh. Each process goes to a
# group by the path its command line names (`<name>.workers/w<k>/...`, or a main checkout `<name>/...`); one that
# names none takes the group of its children (ninja under the jobs it runs), then of its parents (a test under ctest).

PROCESS_KINDS = (
    ("compile", re.compile(r"(^|/)(clang(\+\+)?|c\+\+|cc|gcc|g\+\+|ccache)$")),
    ("link", re.compile(r"(^|/)(ld|ld64|lld|ld\.lld|ar|libtool|ranlib)$")),
    ("build", re.compile(r"(^|/)(ninja|cmake|make)$")),
    ("test", re.compile(r"(^|/)(ctest|[A-Za-z0-9_]*Tests?|BagiStress|BagiMahjong|BagiMatch3|[A-Za-z0-9_]*Probe)$")),
    ("claude", re.compile(r"(^|/)claude$")),
    ("python", re.compile(r"(^|/)python[0-9.]*$")),
)
WORKER_PATH = re.compile(r"/([^/\s'\"]+)\.workers/(w\d+)(?=[/\s'\"]|$)")
INHERIT_DEPTH = 8
# only the processes of a build or a test take a group from a relative (an app or a daemon under launchd never does)
TOOL_KINDS = ("compile", "link", "build", "test", "python")


def process_kind(args: str) -> str:
    first = args.split(" ", 1)[0]
    for kind, pattern in PROCESS_KINDS:
        if pattern.search(first):
            if kind == "compile" and " -c " not in f" {args} " and " -cc1 " not in f" {args} " and " -o " in f" {args} ":
                return "link"  # the compiler driver linking
            if kind == "python" and " -m pytest" in args:
                return "test"
            return kind
    return "other"


def main_checkouts(layout: w.Layout) -> list[tuple[str, str]]:
    """The checkouts beside this one that have a workers folder (`<name>` beside `<name>.workers`), longest path first."""
    found = {str(layout.main): layout.main.name}
    try:
        for folder in layout.root.parent.iterdir():
            if folder.name.endswith(w.WORKERS_SUFFIX):
                main = folder.with_name(folder.name[: -len(w.WORKERS_SUFFIX)])
                if main.is_dir():
                    found[str(main)] = main.name
    except OSError:
        pass
    return sorted(found.items(), key=lambda kv: -len(kv[0]))


def group_of(args: str, mains: list[tuple[str, str]]) -> str | None:
    found = WORKER_PATH.search(args)
    if found:
        return f"{found.group(1)}/{found.group(2)}"
    for path, name in mains:
        if f"{path}/" in args or args.endswith(path):
            return f"{name}/main"
    return None


def read_processes() -> list[dict]:
    try:
        out = subprocess.run(["ps", "-axww", "-o", "pid=,ppid=,pcpu=,rss=,args="], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    rows = []
    for line in out.splitlines():
        parts = line.split(None, 4)
        if len(parts) == 5:
            try:
                rows.append({"pid": int(parts[0]), "ppid": int(parts[1]), "cpu": float(parts[2]), "rss": int(parts[3]) * 1024, "args": parts[4]})
            except ValueError:
                continue
    return rows


def assign_groups(rows: list[dict], mains: list[tuple[str, str]]) -> None:
    """In place: `group` and `kind` of every process (see the section comment)."""
    by_pid = {r["pid"]: r for r in rows}
    children: dict[int, list[dict]] = {}
    for r in rows:
        r["kind"] = process_kind(r["args"])
        r["group"] = None if r["kind"] == "claude" else group_of(r["args"], mains)
        children.setdefault(r["ppid"], []).append(r)
    for r in rows:  # from the children: ninja and cmake name relative paths, their jobs absolute ones
        if r["group"] is None and r["kind"] in TOOL_KINDS and r["pid"] > 1:
            counts: dict[str, int] = {}
            for c in children.get(r["pid"], []):
                if c["group"]:
                    counts[c["group"]] = counts.get(c["group"], 0) + 1
            if counts:
                r["group"] = max(counts, key=counts.get)
    for r in rows:  # from the parents: a job of a tool that named the folder
        parent, depth = by_pid.get(r["ppid"]), 0
        while r["group"] is None and r["kind"] in TOOL_KINDS and parent is not None and parent["pid"] > 1 and depth < INHERIT_DEPTH:
            r["group"] = parent["group"]
            parent, depth = by_pid.get(parent["ppid"]), depth + 1


def memory_state() -> dict | None:
    """macOS: the size, the free share (`kern.memorystatus_level`, what memory_pressure prints), the pressure level
    (1 normal, 2 warn, 4 critical) and the swap in use; None elsewhere."""
    try:
        out = subprocess.run(["sysctl", "-n", "hw.memsize", "kern.memorystatus_level", "kern.memorystatus_vm_pressure_level", "vm.swapusage"],
                             capture_output=True, text=True, timeout=5).stdout.splitlines()
    except (OSError, subprocess.TimeoutExpired):
        return None
    if len(out) < 4:
        return None
    swap = re.search(r"used = ([0-9.]+)M", out[3])
    try:
        return {"total": int(out[0]), "freePercent": int(out[1]), "pressure": {1: "normal", 2: "warn", 4: "critical"}.get(int(out[2]), out[2]),
                "swapUsed": int(float(swap.group(1)) * 1024 * 1024) if swap else None}
    except ValueError:
        return None


def machine_state(layout: w.Layout) -> dict:
    rows = read_processes()
    assign_groups(rows, main_checkouts(layout))
    groups: dict[str, dict] = {}
    claude = {"count": 0, "cpu": 0.0, "rss": 0}
    for r in rows:
        if r["kind"] == "claude" or (r["group"] is None and "/claude" in r["args"].split(" ", 1)[0]):
            claude["count"] += 1
            claude["cpu"] += r["cpu"]
            claude["rss"] += r["rss"]
            continue
        if r["group"] is None:
            continue
        g = groups.setdefault(r["group"], {"group": r["group"], "cpu": 0.0, "rss": 0, "kinds": {}})
        g["cpu"] += r["cpu"]
        g["rss"] += r["rss"]
        if r["kind"] in ("compile", "link", "build", "test"):
            g["kinds"][r["kind"]] = g["kinds"].get(r["kind"], 0) + 1
    try:
        load = list(os.getloadavg())
    except (OSError, AttributeError):
        load = None
    shown = sorted(groups.values(), key=lambda g: -g["cpu"])
    for g in shown:
        g["cpu"] = round(g["cpu"], 1)
    claude["cpu"] = round(claude["cpu"], 1)
    return {"at": int(time.time() * 1000), "cores": os.cpu_count(), "load": load, "memory": memory_state(),
            "checkout": layout.main.name, "groups": shown, "claude": claude}


def cmd_machine(layout: w.Layout, args) -> None:
    state = machine_state(layout)
    if args.json:
        print(json.dumps(state, ensure_ascii=False))
        return
    load = state["load"]
    mem = state["memory"]
    print(f"load {load[0]:.1f} / {state['cores']} cores" if load else "load ?", end="")
    if mem:
        print(f", memory {100 - mem['freePercent']}% used ({mem['pressure']}), swap {(mem['swapUsed'] or 0) / 2**30:.1f} GB", end="")
    print()
    for g in state["groups"]:
        kinds = ", ".join(f"{n} {k}" for k, n in g["kinds"].items())
        print(f"  {g['group']:40} {g['cpu']:6.1f}% {g['rss'] / 2**30:5.1f} GB  {kinds}")
    c = state["claude"]
    print(f"  {'Claude (' + str(c['count']) + ' processes)':40} {c['cpu']:6.1f}% {c['rss'] / 2**30:5.1f} GB")


def add_parsers(sub) -> None:
    """The commands of this module in worker.py's parser."""
    p = sub.add_parser("busy", help="the busy files of the taken entries and which open entries meet them (the panel)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("merge-train", help="merge every ready entry in review one by one, stop at the first conflict (the panel)")
    p.add_argument("--dry-run", action="store_true", help="only the order and what would stop each")
    p.add_argument("--no-kick", action="store_true", help="do not start the idle workers on their queues afterwards")
    p.add_argument("--delay", type=int, default=w.DEFAULT_DELAY_S, help=f"seconds between two starts of the kick (default {w.DEFAULT_DELAY_S})")
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("day", help="what was merged since a time, what waits, what is not pushed (the panel)")
    p.add_argument("--since", help="ISO time or ms since the epoch (default: today 00:00)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("timeline", help="every session of every worker over the last hours, from the transcripts (the panel)")
    p.add_argument("--hours", type=float, default=24)
    p.add_argument("--claude", default="claude", help="the command (default claude)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("machine", help="the load and memory of this machine and what each checkout and worker runs (the panel)")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("review-stats", help="review rounds per entry: the reviewer's CHANGES and the owner's remarks (the panel)")
    p.add_argument("--days", type=float, default=7)
    p.add_argument("--json", action="store_true")


COMMANDS = {"busy": cmd_busy, "merge-train": cmd_merge_train, "day": cmd_day, "timeline": cmd_timeline, "review-stats": cmd_review_stats, "machine": cmd_machine}
