"""A summary of a Chrome trace of the engine's profiler (`--trace=FILE`, `ScopedTrace`). Standard library only.

    python3 tools/perf/trace_summary.py trace.json [--top 25] [--json]

The trace is what `ChromeTraceRecorder` writes (`src/Modules/BECore/Profiler/CLAUDE.md`): `B` / `E` zone pairs per thread (the `E` holds
the allocations of the zone and the zones inside it as `args.allocs` / `args.bytes`), `C` counters, `i` frame marks named `Frame`, `M`
thread names. The summary gives, per zone name over all threads: the calls, the total (inclusive) and the self time (total minus the
zones directly inside), and the inclusive allocations; the frame-time distribution from the gaps between the frame marks; and the mean of
each counter. Unclosed zones at the end (a dump taken mid-run) are left out.
"""

import argparse
import json
import sys
from dataclasses import dataclass, field

FRAME_MARK = "Frame"
MS_PER_US = 0.001


@dataclass
class Zone:
    calls: int = 0
    total_us: float = 0.0
    self_us: float = 0.0
    allocs: int = 0
    bytes: int = 0
    threads: set = field(default_factory=set)


def load_events(path: str) -> list[dict]:
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return data["traceEvents"] if isinstance(data, dict) else data


def percentile(ordered: list[float], fraction: float) -> float:
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))] if ordered else 0.0


def summarize(events: list[dict]) -> dict:
    zones: dict[str, Zone] = {}
    thread_names: dict[int, str] = {}
    stacks: dict[int, list[list]] = {}  # tid -> [name, start, child time]
    frame_marks: list[float] = []
    counters: dict[str, list[float]] = {}
    unclosed = 0
    for event in sorted(events, key=lambda e: (e.get("tid", 0), e.get("ts", 0.0))):
        phase, tid = event.get("ph"), event.get("tid", 0)
        if phase == "M" and event.get("name") == "thread_name":
            thread_names[tid] = event.get("args", {}).get("name", str(tid))
        elif phase == "B":
            stacks.setdefault(tid, []).append([event["name"], event["ts"], 0.0])
        elif phase == "E":
            stack = stacks.get(tid)
            if not stack:
                continue
            name, start, children = stack.pop()
            duration = event["ts"] - start
            zone = zones.setdefault(name, Zone())
            zone.calls += 1
            zone.total_us += duration
            zone.self_us += duration - children
            zone.allocs += int(event.get("args", {}).get("allocs", 0))
            zone.bytes += int(event.get("args", {}).get("bytes", 0))
            zone.threads.add(tid)
            if stack:
                stack[-1][2] += duration
        elif phase == "i" and event.get("name") == FRAME_MARK:
            frame_marks.append(event["ts"])
        elif phase == "C":
            for key, value in event.get("args", {}).items():
                label = event["name"] if key == "value" else f"{event['name']}.{key}"
                counters.setdefault(label, []).append(float(value))
    unclosed = sum(len(stack) for stack in stacks.values())
    frame_marks.sort()
    frames = sorted((b - a) * MS_PER_US for a, b in zip(frame_marks, frame_marks[1:]))
    span = max((e["ts"] for e in events if "ts" in e), default=0.0) - min((e["ts"] for e in events if "ts" in e), default=0.0)
    return {
        "spanMs": span * MS_PER_US,
        "unclosedZones": unclosed,
        "threads": {str(tid): name for tid, name in thread_names.items()},
        "frames": {
            "count": len(frames),
            "meanMs": sum(frames) / len(frames) if frames else 0.0,
            "p50Ms": percentile(frames, 0.50),
            "p95Ms": percentile(frames, 0.95),
            "p99Ms": percentile(frames, 0.99),
            "maxMs": frames[-1] if frames else 0.0,
        },
        "zones": [
            {
                "name": name,
                "calls": zone.calls,
                "totalMs": zone.total_us * MS_PER_US,
                "selfMs": zone.self_us * MS_PER_US,
                "meanUs": zone.total_us / zone.calls,
                "allocs": zone.allocs,
                "bytes": zone.bytes,
                "threads": sorted(thread_names.get(tid, str(tid)) for tid in zone.threads),
            }
            for name, zone in zones.items()
        ],
        "counters": {name: {"samples": len(values), "mean": sum(values) / len(values), "max": max(values)} for name, values in counters.items()},
    }


def format_text(summary: dict, top: int) -> str:
    lines = [f"span {summary['spanMs']:.1f} ms, {len(summary['zones'])} zones, {summary['unclosedZones']} unclosed"]
    frames = summary["frames"]
    if frames["count"]:
        lines.append(
            f"frames {frames['count']}: mean {frames['meanMs']:.3f} ms, p50 {frames['p50Ms']:.3f}, p95 {frames['p95Ms']:.3f}, "
            f"p99 {frames['p99Ms']:.3f}, max {frames['maxMs']:.3f}"
        )
    header = f"{'zone':<40} {'calls':>8} {'self ms':>10} {'total ms':>10} {'mean us':>10} {'allocs':>9} {'bytes':>11}"
    for title, key in (("by self time", "selfMs"), ("by total time", "totalMs"), ("by allocations (inclusive)", "allocs")):
        lines += ["", title, header]
        for zone in sorted(summary["zones"], key=lambda z: z[key], reverse=True)[:top]:
            if key == "allocs" and not zone["allocs"]:
                break
            lines.append(
                f"{zone['name'][:40]:<40} {zone['calls']:>8} {zone['selfMs']:>10.2f} {zone['totalMs']:>10.2f} {zone['meanUs']:>10.2f} "
                f"{zone['allocs']:>9} {zone['bytes']:>11}"
            )
    if summary["counters"]:
        lines += ["", f"{'counter':<40} {'samples':>8} {'mean':>12} {'max':>12}"]
        for name, counter in sorted(summary["counters"].items()):
            lines.append(f"{name[:40]:<40} {counter['samples']:>8} {counter['mean']:>12.2f} {counter['max']:>12.2f}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("trace")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--json", action="store_true", help="the whole summary as JSON")
    args = parser.parse_args()
    summary = summarize(load_events(args.trace))
    print(json.dumps(summary, indent=2) if args.json else format_text(summary, args.top))
    return 0


if __name__ == "__main__":
    sys.exit(main())
