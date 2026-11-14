#!/usr/bin/env python3
"""Reads the `-ftime-trace` files of a build and says where the compile time goes.

Build with the preset `macos-time-trace` (`ENGINE_TIME_TRACE=ON`, cmake/CLAUDE.md, "Compile-time measurement"), then:

    python3 CI/scripts/time_trace_report.py --build build/macos-time-trace [--top 20] [--json] [--only tests/] [--jobs N]

Every `*.json` under `--build` that is a Chrome trace (has a `traceEvents` list) is one compile: a translation unit (TU), or the
precompiled header (a file named `cmake_pch...`, reported on its own). Any other JSON (`compile_commands.json`, caches) is skipped.
A TU is named by the path of its trace file relative to the build folder.

The report holds: the number of TUs and the sum of their `Total ExecuteCompiler` (the time of the compiler process: close to CPU seconds
and steadier than the wall time, which is taken with `time`; it grows a little when the cores are oversubscribed), the sums of `Total
Frontend` and `Total Backend`, the sums of `Total Source` (parsing of included files), `Total ParseClass`, `Total InstantiateClass` and
`Total InstantiateFunction` over the TUs, the PCH compile, the slowest TUs, the headers with the largest **inclusive** `Source` time and
the slowest `ParseClass`, `InstantiateClass` and `InstantiateFunction` names. Inclusive means the time of the header together with
everything it includes, so the lists overlap (a header's total contains the headers below it, an instantiation contains the ones it
causes) and must not be added up. A header is summed over all TUs and shown with the number of TUs that parse it: a header the PCH holds
is parsed once, in the PCH trace, and has its own list. `--only <substring>` keeps the TUs whose trace path contains the substring in
every list (`--only tests/` tells the cost of the tests from the cost of the headers). Clang drops events shorter than 500 us by default,
so small items are not in the lists.

Exit code 0, 2 on a bad argument or a build folder without trace files. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

PCH_MARK = "cmake_pch"
TEMPLATE_EVENTS = ("ParseClass", "InstantiateClass", "InstantiateFunction")
PARTS = ("Source", *TEMPLATE_EVENTS)
POOL_THRESHOLD = 32
MICROSECONDS = 1_000_000.0


def pair_source_events(events: list[dict]) -> dict[str, int]:
    """Inclusive microseconds per header of one trace: `Source` events come as begin/end pairs ('b'/'e', matched per id as a stack)
    or as complete events ('X')."""
    totals: dict[str, int] = defaultdict(int)
    open_events: dict[object, list[dict]] = defaultdict(list)
    for event in events:
        if event.get("name") != "Source":
            continue
        phase = event.get("ph")
        if phase == "X":
            totals[event.get("args", {}).get("detail", "")] += int(event.get("dur", 0))
        elif phase == "b":
            open_events[event.get("id")].append(event)
        elif phase == "e":
            stack = open_events[event.get("id")]
            if stack:
                begin = stack.pop()
                totals[begin.get("args", {}).get("detail", "")] += int(event.get("ts", 0)) - int(begin.get("ts", 0))
    return dict(totals)


def summarize(events: list[dict]) -> dict:
    """One trace as a small dict: the totals, the headers and the template items (all in microseconds)."""
    totals = {"execute": 0, "frontend": 0, "backend": 0}
    for kind in PARTS:
        totals[kind] = 0
    fallback = {"execute": 0, "frontend": 0, "backend": 0}
    wanted = {"Total ExecuteCompiler": "execute", "Total Frontend": "frontend", "Total Backend": "backend"}
    wanted.update({f"Total {kind}": kind for kind in PARTS})
    plain = {"ExecuteCompiler": "execute", "Frontend": "frontend", "Backend": "backend"}
    templates: dict[tuple[str, str], int] = defaultdict(int)
    for event in events:
        name = event.get("name", "")
        if name in wanted:
            totals[wanted[name]] = int(event.get("dur", 0))
        elif name in plain and event.get("ph") == "X":
            fallback[plain[name]] += int(event.get("dur", 0))
        elif name in TEMPLATE_EVENTS and event.get("ph") == "X":
            templates[(name, event.get("args", {}).get("detail", ""))] += int(event.get("dur", 0))
    for key, value in fallback.items():
        if totals[key] == 0:
            totals[key] = value
    return {"totals": totals, "headers": pair_source_events(events), "templates": list(templates.items())}


def load_trace(path: str) -> dict | None:
    """The summary of the trace at `path`, or None when the file is not a Chrome trace."""
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or not isinstance(data.get("traceEvents"), list):
        return None
    summary = summarize(data["traceEvents"])
    summary["path"] = path
    return summary


def find_json_files(build: Path) -> list[str]:
    found = []
    for folder, _dirs, files in os.walk(build):
        for name in files:
            if name.endswith(".json"):
                found.append(os.path.join(folder, name))
    return sorted(found)


def collect(build: Path, only: str, jobs: int) -> list[dict]:
    """The traces under `build` whose path (relative to it) contains `only`; each has `path` relative to `build` and `is_pch`."""
    files = find_json_files(build)
    if len(files) > POOL_THRESHOLD and jobs > 1:
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            loaded = list(pool.map(load_trace, files, chunksize=16))
    else:
        loaded = [load_trace(path) for path in files]
    traces = []
    for summary in loaded:
        if summary is None:
            continue
        relative = os.path.relpath(summary["path"], build).replace(os.sep, "/")
        if only and only not in relative:
            continue
        summary["path"] = relative
        summary["is_pch"] = PCH_MARK in os.path.basename(relative)
        traces.append(summary)
    return traces


def short_path(path: str) -> str:
    """A header path for the table: the SDK prefix cut, a path under the working folder made relative."""
    marker = ".sdk/"
    if marker in path:
        return "<sdk>/" + path.split(marker, 1)[1]
    try:
        return os.path.relpath(path) if os.path.isabs(path) and path.startswith(os.getcwd() + os.sep) else path
    except ValueError:
        return path


def top_headers(traces: list[dict], top: int) -> list[dict]:
    total: dict[str, int] = defaultdict(int)
    count: dict[str, int] = defaultdict(int)
    for trace in traces:
        for header, micro in trace["headers"].items():
            total[header] += micro
            count[header] += 1
    ranked = sorted(total, key=lambda header: -total[header])[:top]
    return [{"header": short_path(h), "seconds": total[h] / MICROSECONDS, "tus": count[h]} for h in ranked]


def top_templates(traces: list[dict], top: int) -> list[dict]:
    total: dict[tuple[str, str], int] = defaultdict(int)
    count: dict[tuple[str, str], int] = defaultdict(int)
    for trace in traces:
        for key, micro in trace["templates"]:
            total[key] += micro
            count[key] += 1
    ranked = sorted(total, key=lambda key: -total[key])[:top]
    return [{"kind": k[0], "name": k[1], "seconds": total[k] / MICROSECONDS, "tus": count[k]} for k in ranked]


def build_report(traces: list[dict], top: int) -> dict:
    units = [t for t in traces if not t["is_pch"]]
    pchs = [t for t in traces if t["is_pch"]]

    def seconds(items: list[dict], key: str) -> float:
        return sum(t["totals"][key] for t in items) / MICROSECONDS

    slowest = sorted(units, key=lambda t: -t["totals"]["execute"])[:top]
    return {
        "tus": len(units),
        "cpu_seconds": seconds(traces, "execute"),
        "tu_cpu_seconds": seconds(units, "execute"),
        "frontend_seconds": seconds(traces, "frontend"),
        "backend_seconds": seconds(traces, "backend"),
        "parts_seconds": {kind: seconds(units, kind) for kind in PARTS},
        "pch": [{"trace": t["path"], "seconds": t["totals"]["execute"] / MICROSECONDS} for t in pchs],
        "slowest_tus": [{"tu": t["path"], "seconds": t["totals"]["execute"] / MICROSECONDS} for t in slowest],
        "headers": top_headers(units, top),
        "pch_headers": top_headers(pchs, top),
        "templates": top_templates(traces, top),
    }


def format_report(report: dict, only: str) -> str:
    lines = []
    scope = f" (only traces with '{only}' in the path)" if only else ""
    lines.append(f"Compile-time report{scope}")
    lines.append(
        f"TUs: {report['tus']}   CPU (Total ExecuteCompiler, all compiles): {report['cpu_seconds']:.1f} s   "
        f"of the TUs: {report['tu_cpu_seconds']:.1f} s"
    )
    lines.append(f"Frontend: {report['frontend_seconds']:.1f} s   Backend: {report['backend_seconds']:.1f} s")
    parts = report["parts_seconds"]
    lines.append("Inside the Frontend of the TUs (inclusive, the parts overlap): " + "   ".join(f"{k}: {v:.1f} s" for k, v in parts.items()))
    if report["pch"]:
        for entry in report["pch"]:
            lines.append(f"PCH compile: {entry['seconds']:.2f} s   {entry['trace']}")
    else:
        lines.append("PCH compile: none")
    lines.append("")
    lines.append("Slowest TUs (s):")
    for entry in report["slowest_tus"]:
        lines.append(f"  {entry['seconds']:7.2f}  {entry['tu']}")
    lines.append("")
    lines.append("Headers outside the PCH, inclusive Source time summed over TUs (s, TUs that parse it):")
    for entry in report["headers"]:
        lines.append(f"  {entry['seconds']:7.2f}  {entry['tus']:5d}  {entry['header']}")
    lines.append("")
    lines.append("Headers of the PCH trace, inclusive (s, TUs):")
    for entry in report["pch_headers"]:
        lines.append(f"  {entry['seconds']:7.2f}  {entry['tus']:5d}  {entry['header']}")
    lines.append("")
    lines.append("Slowest parses and instantiations, summed over TUs (s, TUs):")
    for entry in report["templates"]:
        lines.append(f"  {entry['seconds']:7.2f}  {entry['tus']:5d}  {entry['kind']} {entry['name']}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Report of the -ftime-trace files of a build.")
    parser.add_argument("--build", required=True, help="the build folder (build/macos-time-trace)")
    parser.add_argument("--top", type=int, default=20, help="rows of every list (default 20)")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    parser.add_argument("--only", default="", help="keep the TUs whose trace path contains this substring")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="processes that read the traces")
    try:
        args = parser.parse_args(argv)
    except SystemExit as error:
        return 2 if error.code else 0
    build = Path(args.build)
    if not build.is_dir() or args.top < 1 or args.jobs < 1:
        print(f"error: --build must be a folder, --top and --jobs at least 1: {build}", file=sys.stderr)
        return 2
    traces = collect(build, args.only, args.jobs)
    if not traces:
        print(f"error: no -ftime-trace file under {build}" + (f" matches '{args.only}'" if args.only else ""), file=sys.stderr)
        return 2
    report = build_report(traces, args.top)
    print(json.dumps(report, indent=2) if args.json else format_report(report, args.only))
    return 0


if __name__ == "__main__":
    sys.exit(main())
