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


def judge_count(new: list[float], old: list[float] | None) -> dict:
    """One benchmark with an exact count gated at 0 % beside a time metric under the set's 3 %; `old` None: a baseline written before the count."""
    specs = {"ms": {"better": "lower"}, "draws": {"better": "lower", "thresholdPercent": 0}}
    ms = perf_stats.summarize([10.0] * 3).to_json()
    old_metrics = {"ms": ms} if old is None else {"ms": ms, "draws": perf_stats.summarize(old).to_json()}
    results = {"fake": {"metrics": {"ms": ms, "draws": perf_stats.summarize(new).to_json()}}}
    baseline = {"benchmarks": {"fake": {"metrics": old_metrics}}}
    return perf.compare_with(results, baseline, {"benchmarks": [{"name": "fake", "metrics": specs}]}, 3.0)["fake"]


def test_a_count_under_a_threshold_of_zero_gates_any_change():
    assert judge_count([6842.0] * 3, [6842.0] * 3)["verdict"] == perf_stats.NOISE
    assert judge_count([6843.0] * 3, [6842.0] * 3)["verdict"] == perf_stats.REGRESSION
    assert judge_count([4.0] * 3, [0.0] * 3)["verdict"] == perf_stats.REGRESSION
    assert judge_count([0.0] * 3, [0.0] * 3)["verdict"] == perf_stats.NOISE
    before_the_metric = judge_count([6843.0] * 3, None)
    assert before_the_metric["metrics"]["draws"]["verdict"] == perf_stats.NEW and before_the_metric["verdict"] == perf_stats.NOISE


def test_equal_counts_of_other_run_numbers_are_equal_under_a_threshold_of_zero():
    count = 15826.78  # a mean per frame whose ten copies do not average back to it in floating point
    assert judge_count([count] * 10, [count] * 3)["verdict"] == perf_stats.NOISE


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


def test_the_own_build_of_a_benchmark_is_checked(tmp_path):
    def bench_with(**keys):
        return {"area": "x", "preset": "p", "benchmarks": [{"name": "a", "command": ["c"], "metrics": {"m": {"source": "wall"}}, **keys}]}

    assert perf.load_set(write_rules(tmp_path / "r.md", bench_with(preset="q", profilePreset="r", targets=["T"])))["area"] == "x"
    for keys in ({"preset": 3}, {"preset": ""}, {"profilePreset": ["r"]}, {"targets": "T"}, {"targets": [1]}):
        with pytest.raises(perf.PerfError, match="benchmark 'a': '(preset|profilePreset|targets)' must be"):
            perf.load_set(write_rules(tmp_path / "r.md", bench_with(**keys)))


def test_a_benchmark_with_its_own_preset_runs_its_command_in_that_build():
    set_ = {"preset": "set"}
    own = {"name": "own", "preset": "editor", "command": ["{bin}/Exe"]}
    plain = {"name": "plain", "command": ["{bin}/Exe"]}
    root = Path("/root")
    assert perf.preset_of(own, set_) == "editor" and perf.preset_of(plain, set_) == "set"
    assert perf.expand(own["command"], root, perf.preset_of(own, set_)) == ["/root/build/editor/bin/Exe"]
    assert perf.expand(plain["command"], root, perf.preset_of(plain, set_)) == ["/root/build/set/bin/Exe"]
    assert perf.trace_preset_of(own, {"profilePreset": "prof"}) == "prof"
    assert perf.trace_preset_of({"profilePreset": "own-prof"}, {"profilePreset": "prof"}) == "own-prof"
    assert perf.trace_preset_of(own, set_) == perf.DEFAULT_PROFILE_PRESET


def test_a_whole_run_measures_each_benchmark_in_its_own_folder(tmp_path):
    bench_set = {
        "area": "test",
        "preset": "set",
        "repetitions": 1,
        "benchmarks": [
            {"name": f"b{index}", "warmup": 0, "command": ["{python}", "-c", "import sys; print('{\"n\": %d}' % len(sys.argv[1]))", "{bin}"], "metrics": {"n": {"source": "json", "key": "n"}}, **keys}
            for index, keys in enumerate(({}, {"preset": "editor-build"}))
        ],
    }
    rules = write_rules(tmp_path / "perf-project.md", bench_set)
    done = subprocess.run([sys.executable, str(PERF_DIR / "perf.py"), "--rules", str(rules), "--no-build", "--baseline-file", str(tmp_path / "b.md"), "--json", str(tmp_path / "r.json")],
                          cwd=tmp_path, capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    report = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    lengths = {name: result["metrics"]["n"]["mean"] for name, result in report["results"].items()}
    assert lengths["b1"] - lengths["b0"] == len("editor-build") - len("set")  # `{bin}` is build/<preset>/bin
    assert report["preset"] == "set" and "preset" not in report["results"]["b0"] and report["results"]["b1"]["preset"] == "editor-build"
    listed = subprocess.run([sys.executable, str(PERF_DIR / "perf.py"), "--rules", str(rules), "--list"], cwd=tmp_path, capture_output=True, text=True).stdout.splitlines()
    assert "[editor-build]" in listed[1] and "[" not in listed[0]


def built_by(tmp_path, monkeypatch, set_extra: dict, benches: list[dict], *args: str) -> list[tuple[str, list[str]]]:
    """The calls of `build` of a whole run, which measures nothing real (`build` is a recording stub)."""
    calls = []
    monkeypatch.setattr(perf, "build", lambda root, preset, targets: calls.append((preset, list(targets))))
    monkeypatch.setattr(perf, "commit_of", lambda root: "c1")
    done = [{"name": f"b{index}", "warmup": 0, "command": ["{python}", "-c", "print('{\"ms\": 5}')"], "metrics": {"ms": {"source": "json", "key": "ms"}}, **keys} for index, keys in enumerate(benches)]
    rules = write_rules(tmp_path / "perf-project.md", {"area": "test", "preset": "set", "repetitions": 1, **set_extra, "benchmarks": done})
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--baseline-file", str(tmp_path / "base.md"), *args])
    assert perf.main() == 0
    return calls


def test_the_set_preset_is_not_built_when_every_selected_benchmark_has_its_own(tmp_path, monkeypatch):
    benches = [{}, {"preset": "editor", "targets": ["Stress"]}]
    assert built_by(tmp_path, monkeypatch, {"targets": ["SetTarget"]}, benches, "--only", "b1") == [("editor", ["Stress"])]


def test_two_benchmarks_of_one_own_preset_build_it_once_with_both_targets(tmp_path, monkeypatch):
    benches = [{"preset": "editor", "targets": ["A", "B"]}, {"preset": "editor", "targets": ["B", "C"]}, {"preset": "editor"}]
    calls = built_by(tmp_path, monkeypatch, {"targets": ["SetTarget"]}, benches[:2])
    assert calls == [("editor", ["A", "B", "C"])]
    assert built_by(tmp_path, monkeypatch, {"targets": ["SetTarget"]}, benches[:1] + benches[2:]) == [("editor", ["A", "B", "SetTarget"])]


def test_a_set_without_own_presets_builds_as_before(tmp_path, monkeypatch):
    assert built_by(tmp_path, monkeypatch, {"targets": ["T1", "T2"]}, [{}, {}]) == [("set", ["T1", "T2"])]
    assert built_by(tmp_path, monkeypatch, {}, [{}]) == [("set", [])]


def test_the_trace_builds_the_profile_preset_of_the_benchmark(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(perf, "build", lambda root, preset, targets: calls.append((preset, list(targets))))

    def stop(*_):
        raise perf.PerfError("stop here")

    monkeypatch.setattr(perf, "record_trace", stop)
    bench = {"command": ["c"], "metrics": {"m": {"source": "wall"}}}
    benches = [{"name": "own", "profilePreset": "editor-prof", "targets": ["Stress"], **bench}, {"name": "plain", **bench}]
    rules = write_rules(tmp_path / "perf-project.md", {"area": "test", "preset": "set", "profilePreset": "set-prof", "targets": ["SetTarget"], "benchmarks": benches})
    monkeypatch.chdir(tmp_path)
    for name in ("own", "plain"):
        monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--trace", name])
        assert perf.main() == 1
    assert calls == [("editor-prof", ["Stress"]), ("set-prof", ["SetTarget"])]


def test_a_mixed_set_builds_the_set_preset_and_each_own_preset(tmp_path, monkeypatch):
    benches = [{"preset": "editor", "targets": ["Stress"]}, {}]
    assert built_by(tmp_path, monkeypatch, {"targets": ["SetTarget"]}, benches) == [("editor", ["Stress"]), ("set", ["SetTarget"])]


def test_a_preset_is_a_name_or_one_per_platform(tmp_path, monkeypatch):
    per_os = {"macos": "macos-release", "windows": "windows-clang-release"}
    assert perf.platform_preset("p", "x") == "p"
    assert perf.platform_preset(per_os, "x", "windows") == "windows-clang-release"
    with pytest.raises(perf.PerfError, match="no preset for linux"):
        perf.platform_preset(per_os, "x", "linux")
    for broken in ("", {"macos": ""}, {"macos": 1}, ["macos-release"]):
        with pytest.raises(perf.PerfError, match="must be a preset name"):
            perf.platform_preset(broken, "x", "macos")
    monkeypatch.setattr(perf, "platform_name", lambda: "macos")
    bench = {"name": "a", "command": ["c"], "metrics": {"m": {"source": "wall"}}}
    loaded = perf.load_set(write_rules(tmp_path / "r.md", {"area": "x", "preset": per_os, "profilePreset": {"macos": "macos-profile"}, "benchmarks": [bench]}))
    assert (loaded["preset"], loaded["profilePreset"]) == ("macos-release", "macos-profile")
    with pytest.raises(perf.PerfError, match="'profilePreset' names no preset for macos"):
        perf.load_set(write_rules(tmp_path / "r.md", {"area": "x", "preset": per_os, "profilePreset": {"windows": "w"}, "benchmarks": [bench]}))


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


def test_a_ring_trace_counts_its_frame_instants_and_never_a_frozen_frame():
    """The shape of WriteRingTraceJson: a `Frame` instant at each frame's begin, nested B/E, a thread name; frozen frames are not frames"""

    def begin_of(name, ts):
        return {"name": name, "ph": "B", "pid": 1, "tid": 1, "ts": ts}

    def end_at(ts):
        return {"ph": "E", "pid": 1, "tid": 1, "ts": ts}

    events = [{"name": "thread_name", "ph": "M", "pid": 1, "tid": 1, "args": {"name": "Main"}}]
    for k in range(3):
        begin = k * 16000.0
        events.append({"name": "Frame", "ph": "i", "s": "g", "pid": 1, "tid": 0, "ts": begin, "args": {"frame": k, "ms": 16.0}})
        events += [begin_of("Outer", begin + 1000), begin_of("Inner", begin + 2000), end_at(begin + 5000), end_at(begin + 10000)]
    events.append({"name": "Frozen frame", "ph": "i", "s": "g", "pid": 1, "tid": 0, "ts": 99000.0, "args": {"frame": 9, "ms": 80.0}})

    summary = trace_summary.summarize(events, {"ringFrames": 3, "dropped": 0, "build": "stats"})
    assert summary["frames"]["count"] == 2  # three marks: two gaps
    assert summary["frames"]["maxMs"] == pytest.approx(16.0)
    assert summary["unclosedZones"] == 0 and summary["threads"] == {"1": "Main"}
    zones = {z["name"]: z for z in summary["zones"]}
    assert zones["Outer"]["calls"] == 3 and zones["Outer"]["totalMs"] == pytest.approx(27.0)
    assert zones["Outer"]["selfMs"] == pytest.approx(18.0)  # 9 ms of each call minus the 3 ms of Inner
    assert zones["Inner"]["selfMs"] == pytest.approx(9.0)
    assert "Frozen frame" not in zones


def test_the_trace_summary_prints_allocation_names_and_sizes_only_when_the_trace_has_them(tmp_path):
    events = [{"name": "Z", "ph": "B", "tid": 1, "ts": 0.0}, {"ph": "E", "tid": 1, "ts": 10.0, "args": {"allocs": 1, "bytes": 8}}]
    other = {
        "allocNames": [{"zone": "Z", "name": "vector", "allocs": 3, "bytes": 96}],
        "allocSizes": [{"zone": "Z", "allocs": 4, "buckets": [{"upTo": 32, "allocs": 3}, {"upTo": 0, "allocs": 1}]}],
    }
    path = tmp_path / "trace.json"
    path.write_text(json.dumps({"traceEvents": events, "otherData": other}), encoding="utf-8")
    text = trace_summary.format_text(trace_summary.summarize(*trace_summary.load_trace(str(path))), 5)
    assert "allocations by name" in text and "vector" in text
    assert "allocation sizes" in text and "<=32:3" in text and ">:1" in text

    path.write_text(json.dumps({"traceEvents": events}), encoding="utf-8")
    plain = trace_summary.format_text(trace_summary.summarize(*trace_summary.load_trace(str(path))), 5)
    assert "allocations by name" not in plain and "allocation sizes" not in plain


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


def test_a_skipped_benchmark_is_reported_and_leaves_the_verdicts_and_the_results_alone(tmp_path):
    skipping = "import sys; print('skip: no such backend'); sys.exit(77)"
    bench_set = {
        "area": "test",
        "preset": "none",
        "repetitions": 2,
        "benchmarks": [
            {"name": "gone", "warmup": 0, "command": ["{python}", "-c", skipping], "metrics": {"ms": {"source": "json", "key": "ms"}}},
            {"name": "here", "warmup": 0, "command": ["{python}", "-c", "print('{\"ms\": 5}')"], "metrics": {"ms": {"source": "json", "key": "ms"}}},
        ],
    }
    rules = write_rules(tmp_path / "perf-project.md", bench_set)
    base = [sys.executable, str(PERF_DIR / "perf.py"), "--rules", str(rules), "--no-build", "--baseline-file", str(tmp_path / "baseline.md")]
    done = subprocess.run([*base, "--json", str(tmp_path / "report.json"), "--update-baseline", "--allow-dirty"], cwd=tmp_path, capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    assert "skipped: gone: no such backend" in done.stderr and "gone: SKIPPED (no such backend)" in done.stdout
    report = json.loads((tmp_path / "report.json").read_text(encoding="utf-8"))
    assert report["skipped"] == {"gone": "no such backend"}
    assert set(report["results"]) == {"here"} and set(report["report"]) == {"here"}
    assert "gone" not in (tmp_path / "baseline.md").read_text(encoding="utf-8")


def test_an_exit_code_77_without_a_skip_line_is_a_failed_run(tmp_path):
    bench = {"name": "plain77", "command": ["{python}", "-c", "import sys; sys.exit(77)"], "metrics": {}}
    with pytest.raises(perf.PerfError):
        perf.run_once(bench, tmp_path, "any")


FAKE_TODO = """
import os, sys
args = sys.argv[1:]
with open(os.environ["FAKE_TODO_LOG"], "a", encoding="utf-8") as log:
    log.write(" ".join(args) + "\\n")
down = os.environ.get("FAKE_TODO_DOWN", "")  # "all", or the one command that fails
if down == "all" or (down and down in args):
    print("todo loader: the service did not answer; running the last copy", file=sys.stderr)
    print("todo: the todo service does not answer", file=sys.stderr)
    sys.exit(1)
if "ping" in args:
    print("ok: http://store, areas " + os.environ["FAKE_TODO_AREAS"] + ", live updates on")
elif "get" in args:
    sys.exit(2)
elif "put" in args:
    sys.stdin.read()
    error = os.environ.get("FAKE_TODO_PUT_ERROR")
    if error:
        print(error, file=sys.stderr)
        sys.exit(1)
    print("written")
"""


@pytest.fixture
def fake_store(tmp_path, monkeypatch):
    script = tmp_path / "fake_todo.py"
    script.write_text(FAKE_TODO, encoding="utf-8")
    log = tmp_path / "todo.log"
    log.write_text("", encoding="utf-8")
    monkeypatch.setattr(perf_baseline, "TODO_CLI", script)
    monkeypatch.setenv("FAKE_TODO_LOG", str(log))
    monkeypatch.setenv("FAKE_TODO_AREAS", "engine, inbox, owner")
    monkeypatch.delenv("FAKE_TODO_PUT_ERROR", raising=False)
    monkeypatch.delenv("FAKE_TODO_DOWN", raising=False)
    return log


def run_update(tmp_path, monkeypatch, *extra: str) -> tuple[int, Path]:
    """A whole `--update-baseline` run of area `engine`; the marker file exists once the benchmark has been measured."""
    marker = tmp_path / "measured.txt"
    bench_set = {
        "area": "engine",
        "preset": "none",
        "repetitions": 2,
        "benchmarks": [
            {
                "name": "fake",
                "warmup": 0,
                "command": ["{python}", "-c", f"open({str(marker)!r}, 'a').close(); print('{{\"ms\": 5}}')"],
                "metrics": {"ms": {"source": "json", "key": "ms", "unit": "ms", "better": "lower"}},
            }
        ],
    }
    rules = write_rules(tmp_path / "perf-project.md", bench_set)
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--no-build", "--update-baseline", "--allow-dirty", *extra])
    return perf.main(), marker


def test_a_token_without_the_area_stops_before_measuring(tmp_path, monkeypatch, capsys, fake_store):
    monkeypatch.setenv("FAKE_TODO_AREAS", "match3, inbox")
    code, marker = run_update(tmp_path, monkeypatch)
    err = capsys.readouterr().err
    assert code == 1 and not marker.exists()
    assert "not `engine`" in err and "--env-file" in err and "--baseline-file" in err
    assert "put" not in fake_store.read_text(encoding="utf-8")


def test_a_token_with_the_area_measures_and_writes(tmp_path, monkeypatch, capsys, fake_store):
    code, marker = run_update(tmp_path, monkeypatch)
    assert code == 0 and marker.exists()
    assert "baseline: written" in capsys.readouterr().out


def test_the_env_file_reaches_every_call_of_the_store(tmp_path, monkeypatch, fake_store):
    code, _ = run_update(tmp_path, monkeypatch, "--env-file", "other.env")
    calls = fake_store.read_text(encoding="utf-8").splitlines()
    assert code == 0 and len(calls) == 3 and all(call.startswith("--env-file other.env ") for call in calls)


def test_a_local_baseline_file_needs_no_area(tmp_path, monkeypatch, fake_store):
    monkeypatch.setenv("FAKE_TODO_AREAS", "match3")
    code, marker = run_update(tmp_path, monkeypatch, "--baseline-file", str(tmp_path / "local.md"))
    assert code == 0 and marker.exists() and fake_store.read_text(encoding="utf-8") == ""


@pytest.mark.parametrize("status", [403, 404])
def test_a_refused_write_is_reported_as_a_missing_right(tmp_path, monkeypatch, capsys, fake_store, status):
    monkeypatch.setenv("FAKE_TODO_PUT_ERROR", f"error: PUT /plans/x: HTTP {status}: no plan")
    code, _ = run_update(tmp_path, monkeypatch)
    assert code == 1 and "a missing right, not a missing plan" in capsys.readouterr().err


def test_another_failure_of_the_write_stays_as_it_is(tmp_path, monkeypatch, capsys, fake_store):
    monkeypatch.setenv("FAKE_TODO_PUT_ERROR", "error: the service does not answer")
    code, _ = run_update(tmp_path, monkeypatch)
    err = capsys.readouterr().err
    assert code == 1 and "the service does not answer" in err and "--baseline-file" in err and "missing right" not in err


def measure_report(tmp_path, monkeypatch, value: str = "5") -> tuple[Path, Path, Path]:
    """A plain run of area `engine` with --json; returns the rules, the report and the marker of a measurement."""
    marker = tmp_path / "measured.txt"
    bench_set = {
        "area": "engine",
        "preset": "none",
        "repetitions": 2,
        "benchmarks": [
            {
                "name": "fake",
                "warmup": 0,
                "command": ["{python}", "-c", f"open({str(marker)!r}, 'a').close(); print('{{\"ms\": {value}}}')"],
                "metrics": {"ms": {"source": "json", "key": "ms", "unit": "ms", "better": "lower"}},
            }
        ],
    }
    rules = write_rules(tmp_path / "perf-project.md", bench_set)
    report = tmp_path / "report.json"
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--no-build", "--baseline-file", str(tmp_path / "none.md"), "--json", str(report)])
    assert perf.main() == 0
    marker.unlink()
    return rules, report, marker


def record_report(monkeypatch, rules: Path, report: Path, *extra: str) -> int:
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--update-baseline", "--from-report", str(report), "--allow-dirty", *extra])
    return perf.main()


def test_a_report_is_recorded_without_measuring_again(tmp_path, monkeypatch, fake_store):
    rules, report, marker = measure_report(tmp_path, monkeypatch, "7")
    baseline = tmp_path / "local.md"
    assert record_report(monkeypatch, rules, report, "--baseline-file", str(baseline)) == 0
    assert not marker.exists()
    record = perf_baseline.latest_record(baseline.read_text(encoding="utf-8"))
    assert record["benchmarks"]["fake"]["metrics"]["ms"]["samples"] == [7.0, 7.0]


def test_recording_a_report_checks_the_area_of_the_token(tmp_path, monkeypatch, capsys, fake_store):
    rules, report, _ = measure_report(tmp_path, monkeypatch)
    monkeypatch.setenv("FAKE_TODO_AREAS", "match3")
    assert record_report(monkeypatch, rules, report) == 1
    assert "not `engine`" in capsys.readouterr().err and "put" not in fake_store.read_text(encoding="utf-8")


@pytest.mark.parametrize(
    "change, message",
    [
        (lambda data: data["machine"].update(cores=999), "another machine"),
        (lambda data: data.update(area="match3"), "area match3"),
        (lambda data: data["results"]["fake"]["metrics"].pop("ms"), "not in the set as it is now"),
        (lambda data: data.pop("results"), "without its samples"),
    ],
)
def test_a_report_that_does_not_fit_is_refused(tmp_path, monkeypatch, capsys, fake_store, change, message):
    rules, report, _ = measure_report(tmp_path, monkeypatch)
    data = json.loads(report.read_text(encoding="utf-8"))
    change(data)
    report.write_text(json.dumps(data), encoding="utf-8")
    assert record_report(monkeypatch, rules, report, "--baseline-file", str(tmp_path / "local.md")) == 1
    assert message in capsys.readouterr().err and not (tmp_path / "local.md").exists()


def test_a_dirty_report_needs_allow_dirty(tmp_path, monkeypatch, capsys, fake_store):
    rules, report, _ = measure_report(tmp_path, monkeypatch)
    data = json.loads(report.read_text(encoding="utf-8"))
    data["results"]["fake"]["commit"] = "abc1234-dirty"
    report.write_text(json.dumps(data), encoding="utf-8")
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--update-baseline", "--from-report", str(report), "--baseline-file", str(tmp_path / "local.md")])
    assert perf.main() == 1 and "uncommitted changes" in capsys.readouterr().err


def test_a_report_is_recorded_whole(tmp_path, monkeypatch, capsys, fake_store):
    rules, report, _ = measure_report(tmp_path, monkeypatch)
    assert record_report(monkeypatch, rules, report, "--only", "fake") == 1
    assert "--update-baseline alone" in capsys.readouterr().err


def test_a_plain_run_without_the_store_measures_and_compares_nothing(tmp_path, monkeypatch, capsys, fake_store):
    monkeypatch.setenv("FAKE_TODO_DOWN", "all")
    marker = tmp_path / "measured.txt"
    rules, report, _ = measure_report(tmp_path, monkeypatch)
    capsys.readouterr()
    monkeypatch.setattr(sys, "argv", ["perf.py", "--rules", str(rules), "--no-build", "--json", str(report)])
    assert perf.main() == 0 and marker.exists()
    out, err = capsys.readouterr()
    said = [line for line in err.splitlines() if "unavailable" in line]
    assert len(said) == 1 and "the todo service does not answer" in said[0] and "--baseline-file" in said[0]
    assert "running the last copy" not in err
    assert "(unavailable" in out and "(none yet)" not in out
    data = json.loads(report.read_text(encoding="utf-8"))
    assert "does not answer" in data["baselineError"] and data["report"]["fake"]["metrics"]["ms"]["verdict"] == "new"
    assert "put" not in fake_store.read_text(encoding="utf-8")


def test_an_update_without_the_store_stops_before_measuring(tmp_path, monkeypatch, capsys, fake_store):
    monkeypatch.setenv("FAKE_TODO_DOWN", "all")
    code, marker = run_update(tmp_path, monkeypatch)
    err = capsys.readouterr().err.strip().splitlines()
    assert code == 1 and not marker.exists()
    assert len(err) == 1 and "the todo service does not answer" in err[0] and "--baseline-file" in err[0]


def test_recording_a_report_without_the_store_writes_nothing(tmp_path, monkeypatch, capsys, fake_store):
    rules, report, marker = measure_report(tmp_path, monkeypatch)
    capsys.readouterr()
    monkeypatch.setenv("FAKE_TODO_DOWN", "all")
    assert record_report(monkeypatch, rules, report) == 1 and not marker.exists()
    assert "--baseline-file" in capsys.readouterr().err
    assert "put" not in fake_store.read_text(encoding="utf-8")


def test_a_baseline_that_cannot_be_read_is_never_written_over(tmp_path, monkeypatch, capsys, fake_store):
    monkeypatch.setenv("FAKE_TODO_DOWN", "get")
    code, marker = run_update(tmp_path, monkeypatch)
    assert code == 1 and not marker.exists()
    assert "--baseline-file" in capsys.readouterr().err
    assert "put" not in fake_store.read_text(encoding="utf-8")


def test_a_store_that_hangs_is_unavailable(tmp_path, monkeypatch):
    script = tmp_path / "hangs.py"
    script.write_text("import time; time.sleep(60)", encoding="utf-8")
    monkeypatch.setattr(perf_baseline, "TODO_CLI", script)
    monkeypatch.setattr(perf_baseline, "STORE_TIMEOUT_S", 0.2)
    with pytest.raises(perf_baseline.StoreError, match="no answer"):
        perf_baseline.read_store("perf-baseline-engine-x.md")


def test_a_failed_run_keeps_the_end_of_stdout_and_writes_the_whole_output(tmp_path):
    # gtest prints its list of failures last on stdout while the engine logs to stderr: a shared tail lost the name of the failed test
    script = "import sys; print('[  FAILED  ] Suite.Case'); sys.stderr.write('log line\\n' * 2000); sys.exit(1)"
    bench = {"name": "fails", "command": ["{python}", "-c", script], "metrics": {}}
    with pytest.raises(perf.PerfError) as error:
        perf.run_once(bench, tmp_path, "any")
    assert "[  FAILED  ] Suite.Case" in str(error.value)
    log = tmp_path / "build" / "perf" / "fails-failed.log"
    assert str(log) in str(error.value)
    assert log.read_text(encoding="utf-8").endswith("--- stderr\n" + "log line\n" * 2000)
