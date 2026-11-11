"""The benchmark runner of the perf skill (`.claude/skills/perf/SKILL.md`). Standard library only.

    python3 <engine>/tools/perf/perf.py [--fast] [--only NAME,...] [--repetitions N] [--no-build] [--update-baseline] [--json FILE]

Run from the root of a repository whose `.claude/rules/perf-project.md` holds the benchmark set (a fenced JSON block under
"## Benchmark set"; the format is in `tools/perf/CLAUDE.md`). It builds the set's preset and targets, runs each benchmark N times after
its warm-up runs, reads the metrics each run prints, and compares the means with the baseline of this machine in the todo store
(`perf-baseline-<area>-<machine>.md`). Exit code: 0 no regression, 3 a regression, 1 an error (a failed build or run, a bad set).
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import perf_baseline  # noqa: E402
import perf_stats  # noqa: E402

RULES_FILE = Path(".claude/rules/perf-project.md")
SET_RE = re.compile(r"^## Benchmark set\s*$.*?```json\n(.*?)\n```", re.DOTALL | re.MULTILINE)
PERF_DIR = Path(__file__).resolve().parent
ENGINE_DIR = PERF_DIR.parents[1]
EXIT_REGRESSION = 3
DEFAULT_REPETITIONS = 5
DEFAULT_WARMUP = 1
DEFAULT_THRESHOLD_PERCENT = 3.0
DEFAULT_TIMEOUT_SECONDS = 600
DEFAULT_PROFILE_PRESET = "macos-profile"
SOURCES = ("regex", "json", "wall")


class PerfError(Exception):
    pass


def load_set(rules: Path) -> dict:
    """The benchmark set of the rules file, checked."""
    if not rules.is_file():
        raise PerfError(f"{rules} not found: run from the root of a repository with a benchmark set")
    found = SET_RE.search(rules.read_text(encoding="utf-8"))
    if not found:
        raise PerfError(f"{rules}: no ```json block under '## Benchmark set'")
    try:
        bench_set = json.loads(found.group(1))
    except json.JSONDecodeError as error:
        raise PerfError(f"{rules}: the benchmark set is not JSON: {error}") from error
    for key in ("area", "preset", "benchmarks"):
        if key not in bench_set:
            raise PerfError(f"{rules}: the benchmark set has no '{key}'")
    names = set()
    for bench in bench_set["benchmarks"]:
        name = bench.get("name", "")
        if not name or name in names:
            raise PerfError(f"{rules}: a benchmark without a name or a repeated name: '{name}'")
        names.add(name)
        if not bench.get("command") or not bench.get("metrics"):
            raise PerfError(f"{rules}: benchmark '{name}' needs 'command' and 'metrics'")
        for metric, spec in bench["metrics"].items():
            if spec.get("source") not in SOURCES:
                raise PerfError(f"{rules}: {name}.{metric}: 'source' must be one of {', '.join(SOURCES)}")
            if spec["source"] == "regex" and "pattern" not in spec or spec["source"] == "json" and "key" not in spec:
                raise PerfError(f"{rules}: {name}.{metric}: a regex metric needs 'pattern', a json metric 'key'")
            if spec.get("better", "lower") not in ("lower", "higher"):
                raise PerfError(f"{rules}: {name}.{metric}: 'better' must be 'lower' or 'higher'")
    return bench_set


def expand(command: list[str], root: Path, preset: str, trace: str = "") -> list[str]:
    """The command with `{bin}` (the preset's executables), `{root}`, `{engine}`, `{python}` and `{trace}` (the trace file) put in."""
    values = {"bin": str(root / "build" / preset / "bin"), "root": str(root), "engine": str(ENGINE_DIR), "python": sys.executable, "trace": trace}
    expanded = []
    for part in command:  # not str.format: an argument may hold JSON braces (a control call's params)
        for key, value in values.items():
            part = part.replace("{" + key + "}", value)
        expanded.append(part)
    return expanded


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True).stdout.strip()


def commit_of(root: Path) -> str:
    """The short commit of the repository, `+engine <commit>` when it carries the engine, `-dirty` for uncommitted changes."""
    commit = git(root, "rev-parse", "--short", "HEAD") or "unknown"
    if git(root, "status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all"):
        commit += "-dirty"
    if (root / "engine" / ".git").exists():
        engine = git(root / "engine", "rev-parse", "--short", "HEAD")
        engine_dirty = "-dirty" if git(root / "engine", "status", "--porcelain", "--untracked-files=no") else ""
        commit += f"+engine-{engine}{engine_dirty}"
    return commit


def build(root: Path, preset: str, targets: list[str]) -> None:
    if not (root / "build" / preset / "CMakeCache.txt").is_file():
        subprocess.run(["cmake", "--preset", preset], cwd=root, check=True)
    command = ["cmake", "--build", "--preset", preset] + (["--target", *targets] if targets else [])
    done = subprocess.run(command, cwd=root, capture_output=True, text=True)
    if done.returncode != 0:
        raise PerfError(f"the build failed:\n{done.stdout[-3000:]}{done.stderr[-2000:]}")


def read_metrics(bench: dict, stdout: str, wall: float) -> dict[str, float]:
    values = {}
    json_line = None
    for name, spec in bench["metrics"].items():
        if spec["source"] == "wall":
            values[name] = wall
        elif spec["source"] == "regex":
            found = re.search(spec["pattern"], stdout, re.MULTILINE)
            if not found:
                raise PerfError(f"{bench['name']}.{name}: the pattern {spec['pattern']!r} found nothing in the output")
            values[name] = float(found.group(1))
        else:
            if json_line is None:
                lines = [line for line in stdout.splitlines() if line.startswith("{")]
                if not lines:
                    raise PerfError(f"{bench['name']}: no JSON line in the output")
                json_line = json.loads(lines[-1])
            if spec["key"] not in json_line:
                raise PerfError(f"{bench['name']}.{name}: no key '{spec['key']}' in {sorted(json_line)}")
            values[name] = float(json_line[spec["key"]])
    return values


def run_once(bench: dict, root: Path, preset: str) -> dict[str, float]:
    command = expand(bench["command"], root, preset)
    started = time.perf_counter()
    try:
        done = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=bench.get("timeoutSeconds", DEFAULT_TIMEOUT_SECONDS))
    except subprocess.TimeoutExpired as error:
        raise PerfError(f"{bench['name']}: timed out after {error.timeout} s: {' '.join(command)}") from error
    except OSError as error:
        raise PerfError(f"{bench['name']}: cannot run {' '.join(command)}: {error}") from error
    wall = time.perf_counter() - started
    if done.returncode != 0:
        raise PerfError(f"{bench['name']}: exit code {done.returncode}: {' '.join(command)}\n{(done.stdout + done.stderr)[-2000:]}")
    return read_metrics(bench, done.stdout, wall)


def record_trace(bench: dict, root: Path, preset: str, trace: Path) -> list[str]:
    """Runs the benchmark's `traceCommand` once in the profile build; the trace lands in `trace`. Returns the command."""
    if not bench.get("traceCommand"):
        raise PerfError(f"{bench['name']}: no 'traceCommand' (the program cannot write a trace): use a sampling profiler (perf-investigate)")
    command = expand(bench["traceCommand"], root, preset, str(trace.resolve()))
    done = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=bench.get("timeoutSeconds", DEFAULT_TIMEOUT_SECONDS))
    if done.returncode != 0 or not trace.is_file():
        raise PerfError(f"{bench['name']}: the trace run failed (exit {done.returncode}): {' '.join(command)}\n{(done.stdout + done.stderr)[-2000:]}")
    return command


def measure(bench: dict, root: Path, preset: str, repetitions: int, progress) -> dict[str, list[float]]:
    for _ in range(bench.get("warmup", DEFAULT_WARMUP)):
        run_once(bench, root, preset)
    samples: dict[str, list[float]] = {name: [] for name in bench["metrics"]}
    for index in range(repetitions):
        progress(f"{bench['name']} {index + 1}/{repetitions}")
        for name, value in run_once(bench, root, preset).items():
            samples[name].append(value)
    return samples


def compare_with(results: dict, baseline: dict | None, bench_set: dict, threshold: float) -> dict:
    """Per benchmark: per metric the summary, the baseline's and the comparison; and the benchmark's verdict."""
    specs = {bench["name"]: bench for bench in bench_set["benchmarks"]}
    report = {}
    for name, result in results.items():
        old_bench = (baseline or {}).get("benchmarks", {}).get(name)
        metrics, verdicts = {}, []
        for metric, data in result["metrics"].items():
            spec = specs[name]["metrics"][metric]
            new = perf_stats.Summary.from_json(data)
            row = {"new": data, "unit": spec.get("unit", ""), "better": spec.get("better", "lower"), "gate": spec.get("gate", True)}
            old_data = (old_bench or {}).get("metrics", {}).get(metric)
            if old_data:
                old = perf_stats.Summary.from_json(old_data)
                comparison = perf_stats.compare(new, old, row["better"], spec.get("thresholdPercent", threshold))
                row.update(old=old_data, deltaPercent=comparison.percent, half=comparison.half, verdict=comparison.verdict)
            else:
                row["verdict"] = perf_stats.NEW
            if row["gate"]:
                verdicts.append(row["verdict"])
            metrics[metric] = row
        report[name] = {"verdict": perf_stats.benchmark_verdict(verdicts), "baselineCommit": (old_bench or {}).get("commit"), "metrics": metrics}
    return report


def format_report(report: dict) -> str:
    lines = [f"{'benchmark / metric':<44} {'mean':>12} {'± 95%':>10} {'baseline':>12} {'change':>9}  verdict"]
    for name, bench in report.items():
        base = f" (baseline {bench['baselineCommit']})" if bench["baselineCommit"] else " (no baseline)"
        lines.append(f"{name}{base}: {bench['verdict'].upper()}")
        for metric, row in bench["metrics"].items():
            label = f"  {metric} [{row['unit']}, {row['better']}{'' if row['gate'] else ', info'}]"
            old = f"{row['old']['mean']:>12.4g}" if "old" in row else f"{'-':>12}"
            change = f"{row['deltaPercent']:>+8.1f}%" if "old" in row else f"{'':>9}"
            lines.append(f"{label[:44]:<44} {row['new']['mean']:>12.4g} {row['new']['half']:>10.3g} {old} {change}  {row['verdict']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--rules", default=str(RULES_FILE), help="the rules file with the benchmark set (default .claude/rules/perf-project.md)")
    parser.add_argument("--fast", action="store_true", help="only the benchmarks marked fast (the merge sentinel)")
    parser.add_argument("--only", help="comma-separated benchmark names")
    parser.add_argument("--repetitions", type=int, help="runs per benchmark (default: the benchmark's, else the set's, else 5)")
    parser.add_argument("--no-build", action="store_true", help="use the executables as they are")
    parser.add_argument("--baseline-file", help="compare with this local baseline plan instead of the store's (offline, tests)")
    parser.add_argument("--update-baseline", action="store_true", help="append the results to the baseline in the store (the owner's word only)")
    parser.add_argument("--allow-dirty", action="store_true", help="let --update-baseline record a commit with uncommitted changes")
    parser.add_argument("--json", help="write the full report to this file")
    parser.add_argument("--list", action="store_true", help="print the benchmarks of the set and stop")
    parser.add_argument("--trace", metavar="NAME", help="record a Chrome trace of one benchmark in the set's profile preset and summarize it (perf-investigate)")
    parser.add_argument("--trace-file", help="where --trace writes the trace (default build/perf/<name>-trace.json, git-ignored)")
    args = parser.parse_args()

    root = Path.cwd()
    try:
        bench_set = load_set(root / args.rules)
        benches = bench_set["benchmarks"]
        if args.list:
            for bench in benches:
                print(f"{bench['name']:<28} {'fast' if bench.get('fast') else '    '}  {', '.join(bench['metrics'])}  {bench.get('about', '')}")
            return 0
        if args.trace:
            matches = [bench for bench in benches if bench["name"] == args.trace]
            if not matches:
                raise PerfError(f"unknown benchmark: {args.trace}")
            profile = bench_set.get("profilePreset", DEFAULT_PROFILE_PRESET)
            if not args.no_build:
                print(f"build {profile}", file=sys.stderr)
                build(root, profile, bench_set.get("targets", []))
            trace = Path(args.trace_file) if args.trace_file else root / "build" / "perf" / f"{args.trace}-trace.json"
            trace.parent.mkdir(parents=True, exist_ok=True)
            command = record_trace(matches[0], root, profile, trace)
            print(f"trace of {args.trace} ({profile}, commit {commit_of(root)}): {trace.resolve()}\ncommand: {' '.join(command)}\n")
            import trace_summary  # noqa: PLC0415 - only this mode needs it

            print(trace_summary.format_text(trace_summary.summarize(trace_summary.load_events(str(trace))), 25))
            return 0
        if args.only:
            wanted = set(args.only.split(","))
            unknown = wanted - {bench["name"] for bench in benches}
            if unknown:
                raise PerfError(f"unknown benchmarks: {', '.join(sorted(unknown))}")
            benches = [bench for bench in benches if bench["name"] in wanted]
        if args.fast:
            benches = [bench for bench in benches if bench.get("fast")]
        if not benches:
            raise PerfError("no benchmark to run")
        preset = bench_set["preset"]
        repetitions = args.repetitions or bench_set.get("repetitions", DEFAULT_REPETITIONS)
        commit = commit_of(root)
        if args.update_baseline and "dirty" in commit and not args.allow_dirty:
            raise PerfError(f"the tree has uncommitted changes ({commit}): a baseline names a commit (--allow-dirty to record it anyway)")
        fingerprint = perf_baseline.machine_fingerprint()
        name = perf_baseline.baseline_name(bench_set["area"], perf_baseline.machine_slug(fingerprint))
        if args.baseline_file:
            path = Path(args.baseline_file)
            content = path.read_text(encoding="utf-8") if path.is_file() else None
        else:
            content = perf_baseline.read_store(name)
        baseline = perf_baseline.latest_record(content) if content else None

        if not args.no_build:
            print(f"build {preset}: {', '.join(bench_set.get('targets', [])) or 'all'}", file=sys.stderr)
            build(root, preset, bench_set.get("targets", []))
        results = {}
        for bench in benches:
            runs = args.repetitions or bench.get("repetitions", repetitions)
            samples = measure(bench, root, preset, runs, lambda text: print(text, file=sys.stderr))
            results[bench["name"]] = {
                "commit": commit,
                "repetitions": runs,
                "metrics": {metric: {**perf_stats.summarize(values).to_json(), "samples": values} for metric, values in samples.items()},
            }
        report = compare_with(results, baseline, bench_set, bench_set.get("thresholdPercent", DEFAULT_THRESHOLD_PERCENT))
        print(f"{bench_set['area']} on {perf_baseline.machine_slug(fingerprint)}, {preset}, commit {commit}, {repetitions} runs each, baseline {name}"
              f"{'' if baseline else ' (none yet)'}")
        print(format_report(report))
        if args.json:
            Path(args.json).write_text(json.dumps({"commit": commit, "preset": preset, "machine": fingerprint, "baseline": name, "report": report}, indent=2), encoding="utf-8")
        if args.update_baseline:
            date = datetime.date.today().isoformat()
            record = perf_baseline.merge_record(baseline, results, date, preset, fingerprint)
            updated = perf_baseline.append_record(content, bench_set["area"], fingerprint, record)
            if args.baseline_file:
                Path(args.baseline_file).write_text(updated, encoding="utf-8")
                print(f"baseline written to {args.baseline_file}")
            else:
                print(f"baseline: {perf_baseline.write_store(name, updated)}")
        regressed = [name for name, bench in report.items() if bench["verdict"] == perf_stats.REGRESSION]
        return EXIT_REGRESSION if regressed else 0
    except (PerfError, perf_baseline.StoreError, subprocess.CalledProcessError) as error:
        print(f"perf: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
