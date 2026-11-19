#!/usr/bin/env python3
"""What a todo entry cost in tokens, per role, and how its work went; a history of it that outlives the transcripts.

    python3 tools/tokstats/tokstats.py entry <branch-id> [--entry ID] [--root R] [--json]
    python3 tools/tokstats/tokstats.py record <branch-id> [--entry ID] [--root R]
    python3 tools/tokstats/tokstats.py backfill [--root R] [--since YYYY-MM-DD]
    python3 tools/tokstats/tokstats.py report [--since YYYY-MM] [--area A] [--json]

The transcripts of Claude Code (`<claude home>/projects/<root with every non-alphanumeric as ->/`, the root's worker
folders `<root>.workers/w<k>` included) are read: a main session is the conductor (or the owner), a subagent is its
`agentType` (`todo-classifier`, `todo-architect`, `todo-developer`, `todo-reviewer`). An answer counts for the branch
`todo/<branch-id>` that its record's `gitBranch` names; otherwise (a game's session on an engine entry, a worker on a
detached head) for the branch the session's last Bash command named (`claim --branch`, `switch`, `review --branch`),
until a `todo.py review`, `stop` or `close`. A subagent counts whole for the branch its session held when it started.

The outcome comes from the store (`reviewRounds`, the verdict and the `[blocking:<kind>]` findings of every review
plan, the slice plan's `## Files`) and from git (`<base>...todo/<branch-id>`, or the merge commit once the branch is
gone). `record` writes `<stats dir>/<branch-id>.json` (`TOKSTATS_DIR`, default `<claude home>/todo/tokstats`), the
history that `report` reads; a later record replaces it, unless the transcripts are gone by then.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterable, Optional

ENGINE = Path(__file__).resolve().parents[2]
TODO = ENGINE / "tools" / "todo" / "todo.py"

ROLES = ("classifier", "architect", "developer", "reviewer", "conductor", "other")
ROLE_OF_AGENT = {"todo-classifier": "classifier", "todo-architect": "architect", "todo-developer": "developer", "todo-reviewer": "reviewer"}
USAGE_KEYS = ("input", "cacheWrite", "cacheRead", "output")
# Relative price of a token kind: what a context re-read costs against fresh input and output.
WEIGHTS = {"input": 1.0, "cacheWrite": 1.25, "cacheRead": 0.1, "output": 5.0}
# Dollars per million tokens; the first key that is part of a model id wins. The same table as the worker panel's
# default; `<claude home>/todo/prices.json` (same shape) replaces it.
DEFAULT_PRICES = {
    "fable": {"input": 8, "output": 40, "cacheWrite": 10, "cacheRead": 0.2},
    "mythos": {"input": 8, "output": 40, "cacheWrite": 10, "cacheRead": 0.2},
    "opus": {"input": 4, "output": 20, "cacheWrite": 5, "cacheRead": 0.2},
    "sonnet": {"input": 2, "output": 10, "cacheWrite": 2.5, "cacheRead": 0.1},
    "haiku": {"input": 0.1, "output": 0.5, "cacheWrite": 0.125, "cacheRead": 0.01},
}

# `todo/<id>` as a word of its own: not `tools/todo/todo.py` or `~/.claude/todo/...`, not a file `todo/x.md`
BRANCH_RE = re.compile(r"(?<![\w/.~-])todo/([A-Za-z0-9][A-Za-z0-9_-]*[A-Za-z0-9_]|[A-Za-z0-9])(?![\w./-])")
END_RE = re.compile(r"todo\.py\b[^|;&]*\s(review|stop|close)\s")
BLOCKING_RE = re.compile(r"^\s*-\s*\[blocking(?::(\w+))?\]", re.M)
VERDICT_RE = re.compile(r"^\s*VERDICT:\s*(APPROVE|CHANGES)", re.M)
BASH_KINDS = (
    ("build", r"cmake --build|\bninja\b"), ("configure", r"cmake --preset|cmake -S"), ("ctest", r"\bctest\b"),
    ("todo", r"todo\.py|worker\.py"), ("git-diff", r"\bgit\b.*\b(diff|show|log)\b"), ("git", r"\bgit\b"),
    ("grep", r"\b(rg|grep)\b"), ("read", r"^\s*(cat|sed|head|tail|awk|wc)\b"), ("python", r"\bpython3?\b"),
)


def claude_home() -> Path:
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude").expanduser()


def stats_dir() -> Path:
    return Path(os.environ.get("TOKSTATS_DIR") or claude_home() / "todo" / "tokstats").expanduser()


def folder_name(path: Path) -> str:
    return re.sub(r"[^A-Za-z0-9]", "-", str(path))


def transcript_folders(root: Path, projects: Path) -> list[Path]:
    """The project folders of the root and of its worker folders `<root>.workers/w<k>`."""
    own = folder_name(root.resolve())
    if not projects.is_dir():
        return []
    return sorted(p for p in projects.iterdir() if p.is_dir() and (p.name == own or p.name.startswith(own + "-workers-")))


def bash_kind(command: str) -> str:
    return next((kind for kind, pattern in BASH_KINDS if re.search(pattern, command)), "other")


def counts_of(usage: dict) -> dict:
    return {
        "input": usage.get("input_tokens") or 0,
        "cacheWrite": usage.get("cache_creation_input_tokens") or 0,
        "cacheRead": usage.get("cache_read_input_tokens") or 0,
        "output": usage.get("output_tokens") or 0,
    }


def load_prices() -> dict:
    try:
        own = json.loads((claude_home() / "todo" / "prices.json").read_text())
        return own if isinstance(own, dict) and own else DEFAULT_PRICES
    except (OSError, ValueError):
        return DEFAULT_PRICES


def dollars(model: str, counts: dict, prices: dict) -> float:
    price = next((value for key, value in prices.items() if key in str(model)), None)
    return sum(counts.get(key, 0) * price.get(key, 0) for key in USAGE_KEYS) / 1_000_000 if price else 0.0


def read_records(path: Path) -> list[dict]:
    records = []
    with open(path, errors="replace") as f:
        for line in f:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if isinstance(record, dict):
                records.append(record)
    return records


def own_branch(record: dict) -> Optional[str]:
    match = re.fullmatch(r"todo/(.+)", record.get("gitBranch") or "")
    return match.group(1) if match else None


def tool_uses(record: dict) -> Iterable[dict]:
    content = (record.get("message") or {}).get("content")
    for block in content if isinstance(content, list) else []:
        if isinstance(block, dict) and block.get("type") == "tool_use":
            yield block


def last_text(records: list[dict]) -> str:
    for record in reversed(records):
        if record.get("type") != "assistant":
            continue
        content = (record.get("message") or {}).get("content")
        texts = [b.get("text", "") for b in content if isinstance(b, dict) and b.get("type") == "text"] if isinstance(content, list) else []
        if any(texts):
            return "\n".join(texts)
    return ""


class Run:
    """One transcript's answers, attributed to branches: `turns[branch] = [(model, counts), ...]`."""

    def __init__(self, role: str, session: str):
        self.role = role
        self.session = session
        self.turns: dict[Optional[str], list[tuple[str, dict]]] = defaultdict(list)
        self.tool_bytes: dict[str, int] = defaultdict(int)
        self.bash_bytes: dict[str, int] = defaultdict(int)
        self.text = ""


def scan_main(records: list[dict], session: str) -> tuple[Run, list[tuple[str, Optional[str]]]]:
    """The conductor's run and its timeline `[(timestamp, branch held from then on)]`."""
    run = Run("conductor", session)
    timeline: list[tuple[str, Optional[str]]] = []
    current: Optional[str] = None
    seen: set = set()
    names: dict[str, str] = {}
    for record in records:
        ending = False
        for block in tool_uses(record):
            name = block.get("name") or "?"
            command = (block.get("input") or {}).get("command", "") if name == "Bash" else ""
            names[block.get("id")] = "Bash:" + bash_kind(command) if name == "Bash" else name
            named = BRANCH_RE.findall(command)
            if named:
                current = named[-1]
                timeline.append((record.get("timestamp") or "", current))
            if END_RE.search(command):
                ending = True
        add_turn(run, record, own_branch(record) or current, seen)
        add_tool_results(run, record, names)
        if ending:
            current = None
            timeline.append((record.get("timestamp") or "", None))
    return run, timeline


def add_turn(run: Run, record: dict, branch: Optional[str], seen: set) -> None:
    if record.get("type") != "assistant":
        return
    message = record.get("message") or {}
    usage = message.get("usage")
    if not usage or message.get("id") in seen:
        return
    seen.add(message.get("id"))
    run.turns[branch].append((message.get("model") or "?", counts_of(usage)))


def add_tool_results(run: Run, record: dict, names: dict[str, str]) -> None:
    if record.get("type") != "user":
        return
    content = (record.get("message") or {}).get("content")
    for block in content if isinstance(content, list) else []:
        if isinstance(block, dict) and block.get("type") == "tool_result":
            result = block.get("content")
            size = len(result) if isinstance(result, str) else len(json.dumps(result))
            name = names.get(block.get("tool_use_id"), "?")
            if name.startswith("Bash:"):
                run.bash_bytes[name[5:]] += size
                name = "Bash"
            run.tool_bytes[name] += size


def scan_subagent(records: list[dict], role: str, session: str, held: Optional[str]) -> Run:
    """A subagent counts whole for one branch: the one its records name most, else the one its session held."""
    run = Run(role, session)
    named = [b for b in (own_branch(r) for r in records) if b]
    branch = max(set(named), key=named.count) if named else held
    seen: set = set()
    names: dict[str, str] = {}
    for record in records:
        for block in tool_uses(record):
            name = block.get("name") or "?"
            names[block.get("id")] = "Bash:" + bash_kind((block.get("input") or {}).get("command", "")) if name == "Bash" else name
        add_turn(run, record, branch, seen)
        add_tool_results(run, record, names)
    run.text = last_text(records)
    return run


def held_at(timeline: list[tuple[str, Optional[str]]], timestamp: str) -> Optional[str]:
    branch = None
    for when, held in timeline:
        if when and timestamp and when > timestamp:
            break
        branch = held
    return branch


def scan_folders(folders: list[Path], since: str = "") -> list[Run]:
    runs: list[Run] = []
    for folder in folders:
        for main in sorted(folder.glob("*.jsonl")):
            session = main.stem
            records = read_records(main)
            if since and records and (records[-1].get("timestamp") or "") < since:
                continue
            run, timeline = scan_main(records, session)
            runs.append(run)
            for sub in sorted((folder / session / "subagents").glob("*.jsonl")):
                try:
                    agent = json.loads(sub.with_suffix(".meta.json").read_text()).get("agentType") or ""
                except (OSError, ValueError):
                    agent = ""
                sub_records = read_records(sub)
                start = next((r.get("timestamp") for r in sub_records if r.get("timestamp")), "")
                runs.append(scan_subagent(sub_records, ROLE_OF_AGENT.get(agent, "other"), session, held_at(timeline, start)))
    return runs


def empty_role() -> dict:
    return {"runs": 0, "turns": 0, "peak": 0, "contextMean": 0, **{k: 0 for k in USAGE_KEYS}, "weighted": 0, "dollars": 0.0,
            "models": {}, "toolBytes": {}, "bashBytes": {}, "wpIncomplete": 0}


def tokens_by_branch(runs: list[Run], prices: Optional[dict] = None) -> dict[str, dict[str, dict]]:
    """`{branch-id: {role: totals}}`; answers held by no branch are left out."""
    prices = prices or load_prices()
    out: dict[str, dict[str, dict]] = defaultdict(lambda: defaultdict(empty_role))
    context: dict[tuple[str, str], int] = defaultdict(int)
    for run in runs:
        for branch, turns in run.turns.items():
            if branch is None or not turns:
                continue
            role = out[branch][run.role]
            role["runs"] += 1
            for model, counts in turns:
                size = counts["input"] + counts["cacheWrite"] + counts["cacheRead"]
                role["turns"] += 1
                role["peak"] = max(role["peak"], size)
                context[(branch, run.role)] += size
                for key in USAGE_KEYS:
                    role[key] += counts[key]
                    role["models"].setdefault(model, {k: 0 for k in USAGE_KEYS})[key] += counts[key]
                role["dollars"] += dollars(model, counts, prices)
            # a run that spans several branches (a conductor) lends its tool output to the one it worked on most
            if max(run.turns, key=lambda b: len(run.turns[b]) if b else -1) == branch:
                for name, size in run.tool_bytes.items():
                    role["toolBytes"][name] = role["toolBytes"].get(name, 0) + size
                for name, size in run.bash_bytes.items():
                    role["bashBytes"][name] = role["bashBytes"].get(name, 0) + size
            if run.role == "developer" and "WP incomplete" in run.text:
                role["wpIncomplete"] += 1
    for branch, roles in out.items():
        for name, role in roles.items():
            role["weighted"] = round(sum(role[k] * WEIGHTS[k] for k in USAGE_KEYS))
            role["contextMean"] = round(context[(branch, name)] / role["turns"]) if role["turns"] else 0
            role["dollars"] = round(role["dollars"], 2)
    return {b: dict(r) for b, r in out.items()}


# --- the outcome: the store and git ------------------------------------------------------------------------------

Store = Callable[..., Optional[str]]


def todo_cli(*args: str) -> Optional[str]:
    """The store's answer, or None when it refuses or does not answer (the outcome is then partial, never a stop)."""
    try:
        done = subprocess.run([sys.executable, str(TODO), "--actor", "tokstats", *args], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout if done.returncode == 0 else None


def git(repo: Path, *args: str) -> Optional[str]:
    try:
        done = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout if done.returncode == 0 else None


def entry_of(branch_id: str, store: Store) -> Optional[dict]:
    """The entry of a branch: `<id>`, or `<id>` of `<id>-<n>` (the next slice of an entry)."""
    for candidate in (branch_id, re.sub(r"-\d+$", "", branch_id)):
        text = store("show", candidate, "--json")
        if text:
            try:
                return json.loads(text)
            except ValueError:
                return None
    return None


def review_outcome(entry_id: str, branch_id: str, store: Store) -> dict:
    out = {"reviews": [], "blockingByKind": {}}
    text = store("plan", "list", "--entry", entry_id, "--json")
    try:
        plans = json.loads(text) if text else []
    except ValueError:
        plans = []
    for plan in plans:
        name = plan.get("name") or ""
        if plan.get("kind") != "review" or not name.startswith(f"todo-loop-{branch_id}-review"):
            continue
        body = store("plan", "get", name, "--stdout") or ""
        verdict = VERDICT_RE.search(body)
        kinds = [kind or "unknown" for kind in BLOCKING_RE.findall(body)]
        for kind in kinds:
            out["blockingByKind"][kind] = out["blockingByKind"].get(kind, 0) + 1
        out["reviews"].append({"name": name, "verdict": verdict.group(1) if verdict else None, "blocking": len(kinds)})
    out["planFiles"] = planned_files(store("plan", "get", f"todo-loop-{branch_id}.md", "--stdout") or "")
    return out


def planned_files(plan: str) -> list[str]:
    match = re.search(r"^## Files\s*\n(.*?)(?=^## |\Z)", plan, re.M | re.S)
    if not match:
        return []
    return [m.strip("` ") for m in re.findall(r"^\s*-\s+`?([^\s`]+)`?", match.group(1), re.M)]


def diff_range(repo: Path, branch_id: str, base: Optional[str]) -> Optional[str]:
    """`<base>...todo/<id>` while the branch exists, else the merge commit `M^1...M^2`."""
    if git(repo, "rev-parse", "--verify", "--quiet", f"refs/heads/todo/{branch_id}") is not None and base:
        return f"{base}...todo/{branch_id}"
    merges = git(repo, "log", "--merges", "--format=%H", "-1", "--grep", f"^Merge branch 'todo/{branch_id}'$")
    return f"{merges.strip()}^1...{merges.strip()}^2" if merges and merges.strip() else None


def git_outcome(repos: list[Path], branch_id: str, base: Optional[str]) -> dict:
    for repo in repos:
        span = diff_range(repo, branch_id, base)
        if not span:
            continue
        names = (git(repo, "diff", "--name-only", span) or "").split()
        stat = git(repo, "diff", "--shortstat", span) or ""
        added = re.search(r"(\d+) insertion", stat)
        removed = re.search(r"(\d+) deletion", stat)
        return {"repo": str(repo), "range": span, "files": names,
                "lines": (int(added.group(1)) if added else 0) + (int(removed.group(1)) if removed else 0)}
    return {}


def plan_accuracy(planned: list[str], changed: list[str]) -> Optional[dict]:
    if not planned or not changed:
        return None
    hit = [p for p in planned if p in changed or any(c.startswith(p.rstrip("/") + "/") for c in changed)]
    unplanned = [c for c in changed if c not in planned and not any(c.startswith(p.rstrip("/") + "/") for p in planned)]
    return {"planned": len(planned), "changed": len(changed), "planHit": len(hit), "unplanned": len(unplanned)}


def build_record(branch_id: str, tokens: dict[str, dict], store: Store, repos: list[Path], entry_id: Optional[str] = None) -> dict:
    entry = entry_of(entry_id or branch_id, store) or {}
    entry_id = entry.get("id") or entry_id or branch_id
    outcome = review_outcome(entry_id, branch_id, store)
    code = git_outcome(repos, branch_id, entry.get("base") or "main")
    roles = tokens.get(branch_id, {})
    total = {k: sum(r.get(k, 0) for r in roles.values()) for k in (*USAGE_KEYS, "weighted", "dollars", "turns")}
    total["dollars"] = round(total["dollars"], 2)
    return {
        "branch": branch_id, "entry": entry_id, "area": entry.get("area"), "title": entry.get("title"),
        "closedAt": entry.get("closedAt"), "status": entry.get("status"),
        "recordedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "total": total, "roles": roles,
        "reviewRounds": entry.get("reviewRounds"), "reviews": outcome["reviews"], "blockingByKind": outcome["blockingByKind"],
        "wpIncomplete": sum(r.get("wpIncomplete", 0) for r in roles.values()),
        "lines": code.get("lines"), "plan": plan_accuracy(outcome["planFiles"], code.get("files") or []),
    }


def save_record(record: dict) -> Path:
    folder = stats_dir()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{record['branch']}.json"
    try:
        old = json.loads(path.read_text())
    except (OSError, ValueError):
        old = None
    if old and not record["roles"] and old.get("roles"):
        record = {**record, "roles": old["roles"], "total": old["total"], "wpIncomplete": old.get("wpIncomplete", 0)}
    path.write_text(json.dumps(record, indent=1, sort_keys=True) + "\n")
    return path


def load_records() -> list[dict]:
    out = []
    for path in sorted(stats_dir().glob("*.json")):
        try:
            out.append(json.loads(path.read_text()))
        except (OSError, ValueError):
            continue
    return out


# --- report ------------------------------------------------------------------------------------------------------

def month_of(record: dict) -> str:
    return (record.get("closedAt") or record.get("recordedAt") or "")[:7] or "?"


def summarize(records: list[dict]) -> dict:
    weighted = sorted(r["total"]["weighted"] for r in records)
    roles = {role: sum(r["roles"].get(role, {}).get("weighted", 0) for r in records) for role in ROLES}
    total = sum(roles.values()) or 1
    rounds = [len(r.get("reviews") or []) for r in records]
    kinds: dict[str, int] = defaultdict(int)
    for r in records:
        for kind, n in (r.get("blockingByKind") or {}).items():
            kinds[kind] += n
    hits = [r["plan"]["planHit"] / r["plan"]["changed"] for r in records if r.get("plan")]
    return {
        "entries": len(records),
        "weightedMedian": round(statistics.median(weighted)) if weighted else 0,
        "weightedP90": weighted[min(len(weighted) - 1, int(len(weighted) * 0.9))] if weighted else 0,
        "dollars": round(sum(r["total"]["dollars"] for r in records), 2),
        "roleShare": {role: round(100 * v / total) for role, v in roles.items() if v},
        "reviewsMean": round(statistics.mean(rounds), 2) if rounds else 0,
        "twoPlusReviews": sum(1 for n in rounds if n >= 2),
        "blockingByKind": dict(kinds),
        "wpIncomplete": sum(r.get("wpIncomplete", 0) for r in records),
        "planHitMean": round(statistics.mean(hits), 2) if hits else None,
    }


def report(records: list[dict], since: str = "", area: str = "") -> dict:
    groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for record in records:
        if record.get("total", {}).get("weighted") and month_of(record) >= since and (not area or record.get("area") == area):
            groups[(record.get("area") or "?", month_of(record))].append(record)
    return {f"{a} {m}": summarize(rs) for (a, m), rs in sorted(groups.items())}


def print_entry(record: dict) -> None:
    print(f"{record['branch']} ({record.get('area')}): {record['total']['weighted'] / 1e6:.1f}M weighted, ${record['total']['dollars']}, "
          f"{record['total']['turns']} turns, {len(record.get('reviews') or [])} reviews, lines {record.get('lines')}")
    print(f"  {'role':<11}{'runs':>5}{'turns':>7}{'peak':>8}{'mean':>8}{'weighted':>10}{'$':>8}")
    for role in ROLES:
        r = record["roles"].get(role)
        if r:
            print(f"  {role:<11}{r['runs']:>5}{r['turns']:>7}{r['peak'] // 1000:>7}k{r['contextMean'] // 1000:>7}k"
                  f"{r['weighted'] / 1e6:>9.2f}M{r['dollars']:>8.2f}")
    for review in record.get("reviews") or []:
        print(f"  review {review['name']}: {review['verdict']}, {review['blocking']} blocking")
    if record.get("blockingByKind"):
        print(f"  blocking by kind: {record['blockingByKind']}")
    if record.get("plan"):
        print(f"  plan: {record['plan']}")


def print_report(rows: dict) -> None:
    for key, s in rows.items():
        print(f"{key}: {s['entries']} entries, median {s['weightedMedian'] / 1e6:.1f}M, p90 {s['weightedP90'] / 1e6:.1f}M, "
              f"${s['dollars']}, reviews {s['reviewsMean']} per entry ({s['twoPlusReviews']} with 2+), "
              f"WP incomplete {s['wpIncomplete']}, plan hit {s['planHitMean']}")
        print(f"  roles %: {s['roleShare']}  blocking: {s['blockingByKind']}")


def repos_of(root: Path) -> list[Path]:
    return [root] + ([root / "engine"] if (root / "engine" / ".git").exists() else [])


def main(argv: Optional[list[str]] = None, store: Store = todo_cli) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("entry", "record"):
        p = sub.add_parser(name)
        p.add_argument("branch", help="the branch id (todo/<branch-id>)")
        p.add_argument("--entry", help="the entry id, when it is not the branch id or the branch id without -<n>")
        p.add_argument("--root", type=Path, default=Path.cwd(), help="the checkout the loops ran in (default: here)")
        p.add_argument("--json", action="store_true")
    p = sub.add_parser("backfill", help="record every branch the transcripts name that has no record yet")
    p.add_argument("--root", type=Path, default=Path.cwd())
    p.add_argument("--since", default="", help="skip sessions that ended before this date (YYYY-MM-DD)")
    p = sub.add_parser("report")
    p.add_argument("--since", default="", help="YYYY-MM")
    p.add_argument("--area", default="")
    p.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    if args.command == "report":
        rows = report(load_records(), args.since, args.area)
        print(json.dumps(rows, indent=1)) if args.json else print_report(rows)
        return 0
    folders = transcript_folders(args.root, claude_home() / "projects")
    if not folders:
        print(f"tokstats: no transcripts of {args.root} under {claude_home() / 'projects'}", file=sys.stderr)
    tokens = tokens_by_branch(scan_folders(folders, getattr(args, "since", "")))
    if args.command == "backfill":
        done = {path.stem for path in stats_dir().glob("*.json")}
        for branch in sorted(set(tokens) - done):
            if not entry_of(branch, store):
                print(f"{branch}: no such entry in the store, skipped")
                continue
            record = build_record(branch, tokens, store, repos_of(args.root))
            save_record(record)
            print(f"{branch}: {record['total']['weighted'] / 1e6:.1f}M weighted, {len(record['reviews'])} reviews")
        return 0
    record = build_record(args.branch, tokens, store, repos_of(args.root), args.entry)
    if args.command == "record":
        path = save_record(record)
        if not record["roles"]:
            print(f"tokstats: no transcript names todo/{args.branch}; the outcome is recorded alone", file=sys.stderr)
        print(f"{path}: {record['total']['weighted'] / 1e6:.1f}M weighted, ${record['total']['dollars']}")
        return 0
    print(json.dumps(record, indent=1)) if args.json else print_entry(record)
    return 0


if __name__ == "__main__":
    sys.exit(main())
