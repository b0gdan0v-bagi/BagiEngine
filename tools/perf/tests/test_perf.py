"""The pure parts of the perf runner: statistics, verdicts, the baseline plan, the benchmark set, the trace summary, and one whole run
of perf.py over a fake benchmark (a Python one-liner) with a local baseline file."""

import json
import subprocess
import sys
from pathlib import Path

import pytest

PERF_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PERF_DIR))

import perf  # noqa: E402
import perf_baseline  # noqa: E402
import perf_stats  # noqa: E402
import trace_summary  # noqa: E402


def test_t95_matches_the_table_and_goes_to_the_normal_value():
    assert perf_stats.t95(4) == pytest.approx(2.776)
    assert perf_stats.t95(4.5) == pytest.approx((2.776 + 2.571) / 2)
    assert perf_stats.t95(120) == pytest.approx(1.980)
    assert 1.96 < perf_stats.t95(10_000) < 1.961


def test_summarize_gives_the_t_interval():
    summary = perf_stats.summarize([10.0, 12.0, 11.0, 9.0, 13.0])
    assert summary.n == 5 and summary.mean == pytest.approx(11.0)
    assert summary.sd == pytest.approx(1.5811, abs=1e-4)
    assert summary.half == pytest.approx(2.776 * 1.5811 / 5**0.5, abs=1e-3)
    assert perf_stats.summarize([4.0]).half == 0.0
    assert perf_stats.summarize([]).n == 0


def test_a_clear_slowdown_is_a_regression_and_a_speedup_an_improvement():
    old = perf_stats.summarize([100.0, 101.0, 99.0, 100.5, 99.5])
    slow = perf_stats.summarize([110.0, 111.0, 109.0, 110.5, 109.5])
    assert perf_stats.compare(slow, old, "lower", 3.0).verdict == perf_stats.REGRESSION
    assert perf_stats.compare(slow, old, "higher", 3.0).verdict == perf_stats.IMPROVEMENT
    assert perf_stats.compare(old, slow, "lower", 3.0).verdict == perf_stats.IMPROVEMENT


def test_a_change_inside_the_interval_or_under_the_threshold_is_noise():
    old = perf_stats.summarize([100.0, 120.0, 80.0, 110.0, 90.0])
    new = perf_stats.summarize([105.0, 125.0, 85.0, 115.0, 95.0])
    assert perf_stats.compare(new, old, "lower", 3.0).verdict == perf_stats.NOISE  # +5 % inside a wide interval
    tight_old = perf_stats.summarize([100.0, 100.1, 99.9])
    tight_new = perf_stats.summarize([102.0, 102.1, 101.9])
    comparison = perf_stats.compare(tight_new, tight_old, "lower", 3.0)
    assert comparison.half < comparison.delta and comparison.verdict == perf_stats.NOISE  # significant, but only +2 %


def test_deterministic_series_differ_by_the_threshold_alone():
    assert perf_stats.compare(perf_stats.summarize([110.0] * 3), perf_stats.summarize([100.0] * 3), "lower", 3.0).verdict == perf_stats.REGRESSION
    assert perf_stats.compare(perf_stats.summarize([100.0] * 3), perf_stats.summarize([100.0] * 3), "lower", 3.0).verdict == perf_stats.NOISE


def test_a_benchmark_takes_its_worst_verdict():
    assert perf_stats.benchmark_verdict(["noise", "improvement", "regression"]) == "regression"
    assert perf_stats.benchmark_verdict(["noise", "improvement", "new"]) == "improvement"
    assert perf_stats.benchmark_verdict(["new"]) == "new"


def test_the_slug_and_the_plan_name_are_stable_and_bare():
    fingerprint = {"model": "Mac16,7", "cpu": "Apple M4 Pro", "cores": 14, "memoryGb": 48, "os": "macos", "osMajor": "26"}
    assert perf_baseline.machine_slug(fingerprint) == "mac16-7-14c-macos26"
    assert perf_baseline.baseline_name("mahjong", "mac16-7-14c-macos26") == "perf-baseline-mahjong-mac16-7-14c-macos26.md"


def test_a_baseline_keeps_its_history_and_the_last_record_wins():
    fingerprint = {"model": "M", "cpu": "C", "cores": 2, "memoryGb": 8, "os": "macos", "osMajor": "26"}
    first = perf_baseline.merge_record(None, {"a": {"commit": "c1", "metrics": {}}, "b": {"commit": "c1", "metrics": {}}}, "2026-11-10", "p", fingerprint)
    text = perf_baseline.append_record(None, "engine", fingerprint, first)
    assert text.startswith("# Perf baseline: engine on m-2c-macos26")
    second = perf_baseline.merge_record(perf_baseline.latest_record(text), {"a": {"commit": "c2", "metrics": {}}}, "2026-11-11", "p", fingerprint)
    text = perf_baseline.append_record(text, "engine", fingerprint, second)
    assert text.count("```json") == 2 and "## 2026-11-11 c1, c2 (p)" in text
    latest = perf_baseline.latest_record(text)
    assert latest["benchmarks"]["a"]["commit"] == "c2" and latest["benchmarks"]["b"]["commit"] == "c1"
    assert perf_baseline.latest_record("# nothing") is None


def write_rules(path: Path, bench_set: dict) -> Path:
    path.write_text(f"# Benchmarks\n\n## Benchmark set\n\n```json\n{json.dumps(bench_set)}\n```\n", encoding="utf-8")
    return path


def test_the_set_is_checked(tmp_path):
    good = {"area": "x", "preset": "p", "benchmarks": [{"name": "a", "command": ["c"], "metrics": {"m": {"source": "wall"}}}]}
    assert perf.load_set(write_rules(tmp_path / "r.md", good))["area"] == "x"
    for broken, message in (
        ({"area": "x", "preset": "p"}, "no 'benchmarks'"),
        ({**good, "benchmarks": good["benchmarks"] * 2}, "repeated name"),
        ({**good, "benchmarks": [{"name": "a", "command": ["c"], "metrics": {"m": {"source": "time"}}}]}, "'source' must be"),
        ({**good, "benchmarks": [{"name": "a", "command": ["c"], "metrics": {"m": {"source": "regex"}}}]}, "needs 'pattern'"),
        ({**good, "benchmarks": [{"name": "a", "command": ["c"], "metrics": {"m": {"source": "wall", "better": "less"}}}]}, "'better' must"),
    ):
        with pytest.raises(perf.PerfError, match=message):
            perf.load_set(write_rules(tmp_path / "r.md", broken))
    (tmp_path / "none.md").write_text("# no set\n", encoding="utf-8")
    with pytest.raises(perf.PerfError, match="no ```json block"):
        perf.load_set(tmp_path / "none.md")


def test_metrics_are_read_from_a_pattern_the_last_json_line_and_the_wall_time():
    bench = {
        "name": "b",
        "metrics": {
            "us": {"source": "regex", "pattern": r"^normal\s+\d+\s+([0-9.]+)$"},
            "fps": {"source": "json", "key": "fps"},
            "wall": {"source": "wall"},
        },
    }
    out = 'player decisions us\nnormal   344   294.36\n{"fps": 1}\n{"fps": 170.5}\n'
    assert perf.read_metrics(bench, out, 2.5) == {"us": 294.36, "fps": 170.5, "wall": 2.5}
    with pytest.raises(perf.PerfError, match="found nothing"):
        perf.read_metrics(bench, '{"fps": 1}', 1.0)


def test_the_trace_summary_gives_self_time_allocations_and_frames():
    events = [
        {"name": "thread_name", "ph": "M", "tid": 1, "args": {"name": "Main"}},
        {"name": "Frame", "ph": "i", "tid": 1, "ts": 0.0},
        {"name": "Outer", "ph": "B", "tid": 1, "ts": 0.0},
        {"name": "Inner", "ph": "B", "tid": 1, "ts": 100.0},
        {"ph": "E", "tid": 1, "ts": 400.0, "args": {"allocs": 2, "bytes": 64}},
        {"ph": "E", "tid": 1, "ts": 1000.0, "args": {"allocs": 3, "bytes": 96}},
        {"name": "Frame", "ph": "i", "tid": 1, "ts": 1000.0},
        {"name": "Inner", "ph": "B", "tid": 2, "ts": 0.0},
        {"ph": "E", "tid": 2, "ts": 100.0, "args": {"allocs": 0, "bytes": 0}},
        {"name": "Open", "ph": "B", "tid": 2, "ts": 200.0},
        {"name": "Draw calls", "ph": "C", "tid": 1, "ts": 1000.0, "args": {"value": 10}},
        {"name": "Frame", "ph": "i", "tid": 1, "ts": 3000.0},
    ]
    summary = trace_summary.summarize(events)
    zones = {zone["name"]: zone for zone in summary["zones"]}
    assert zones["Outer"]["totalMs"] == pytest.approx(1.0) and zones["Outer"]["selfMs"] == pytest.approx(0.7)
    assert zones["Inner"]["calls"] == 2 and zones["Inner"]["totalMs"] == pytest.approx(0.4) and zones["Inner"]["allocs"] == 2
    assert zones["Inner"]["threads"] == ["2", "Main"]
    assert summary["unclosedZones"] == 1
    assert summary["frames"]["count"] == 2 and summary["frames"]["maxMs"] == pytest.approx(2.0)
    assert summary["counters"]["Draw calls"]["mean"] == 10
    assert "Outer" in trace_summary.format_text(summary, 5)


def test_a_whole_run_compares_with_a_local_baseline_and_exits_3_on_a_regression(tmp_path):
    value = tmp_path / "value.txt"
    value.write_text("100", encoding="utf-8")
    bench_set = {
        "area": "test",
        "preset": "none",
        "repetitions": 3,
        "benchmarks": [
            {
                "name": "fake",
                "fast": True,
                "warmup": 0,
                "command": ["{python}", "-c", f"print('{{\"ms\": ' + open({str(value)!r}).read() + '}}')"],
                "metrics": {"ms": {"source": "json", "key": "ms", "unit": "ms", "better": "lower"}},
            }
        ],
    }
    rules = write_rules(tmp_path / "perf-project.md", bench_set)
    baseline = tmp_path / "baseline.md"
    base_args = [sys.executable, str(PERF_DIR / "perf.py"), "--rules", str(rules), "--no-build", "--baseline-file", str(baseline)]

    def run(*extra):
        return subprocess.run([*base_args, *extra], cwd=tmp_path, capture_output=True, text=True)

    first = run("--update-baseline", "--allow-dirty")
    assert first.returncode == 0, first.stderr
    assert "new" in first.stdout and baseline.is_file()
    assert run("--fast").returncode == 0
    value.write_text("120", encoding="utf-8")
    slow = run("--json", str(tmp_path / "report.json"))
    assert slow.returncode == perf.EXIT_REGRESSION and "REGRESSION" in slow.stdout
    report = json.loads((tmp_path / "report.json").read_text(encoding="utf-8"))
    assert report["report"]["fake"]["metrics"]["ms"]["deltaPercent"] == pytest.approx(20.0)
    assert run("--only", "missing").returncode == 1
