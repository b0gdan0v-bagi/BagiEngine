"""time_trace_report.py on small hand-written traces in a temp folder."""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import time_trace_report as report  # noqa: E402


def source(header: str, begin: int, end: int) -> list[dict]:
    return [
        {"ph": "b", "id": 0, "ts": begin, "name": "Source", "args": {"detail": header}},
        {"ph": "e", "id": 0, "ts": end, "name": "Source"},
    ]


def total(name: str, micro: int) -> dict:
    return {"ph": "X", "ts": 0, "dur": micro, "name": f"Total {name}", "args": {"count": 1}}


def item(name: str, detail: str, micro: int) -> dict:
    return {"ph": "X", "ts": 10, "dur": micro, "name": name, "args": {"detail": detail}}


def write_trace(build: Path, relative: str, events: list[dict]) -> None:
    path = build / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"traceEvents": events, "beginningOfTime": 0}))


def make_build(tmp_path: Path) -> Path:
    build = tmp_path / "build"
    write_trace(
        build,
        "src/CMakeFiles/Mod.dir/cmake_pch.hxx.json",
        source("/inc/pch.h", 0, 4_000_000) + source("/inc/Heavy.h", 100, 3_000_000) + [total("ExecuteCompiler", 5_000_000)],
    )
    write_trace(
        build,
        "src/CMakeFiles/Mod.dir/A.cpp.json",
        source("/inc/Own.h", 100, 2_100) + source("/inc/Own.h", 3_000, 4_000) + source("/inc/Other.h", 5_000, 6_000)
        + [total("ExecuteCompiler", 3_000_000), total("Frontend", 2_000_000), total("Backend", 1_000_000), total("Source", 1_500_000)]
        + [item("InstantiateFunction", "std::vector<int>::push_back", 700_000), item("ParseClass", "Foo", 100_000)],
    )
    write_trace(
        build,
        "tests/CMakeFiles/T.dir/BTest.cpp.json",
        source("/inc/Own.h", 0, 500) + [total("ExecuteCompiler", 7_000_000), item("InstantiateFunction", "std::vector<int>::push_back", 300_000)],
    )
    (build / "compile_commands.json").write_text("[]")
    (build / "other.json").write_text('{"a": 1}')
    (build / "broken.json").write_text("{")
    return build


def test_source_pairs_give_inclusive_time_per_header():
    events = source("a.h", 0, 100) + source("b.h", 10, 30) + source("a.h", 200, 250)
    assert report.pair_source_events(events) == {"a.h": 150, "b.h": 20}


def test_report_counts_tus_cpu_and_pch(tmp_path):
    build = make_build(tmp_path)
    result = report.build_report(report.collect(build, "", 1), 5)
    assert result["tus"] == 2
    assert result["cpu_seconds"] == 15.0
    assert result["tu_cpu_seconds"] == 10.0
    assert result["frontend_seconds"] == 2.0
    assert result["backend_seconds"] == 1.0
    assert result["parts_seconds"]["Source"] == 1.5
    assert [entry["seconds"] for entry in result["pch"]] == [5.0]
    assert result["slowest_tus"][0]["tu"] == "tests/CMakeFiles/T.dir/BTest.cpp.json"


def test_headers_are_summed_over_tus_with_their_count(tmp_path):
    build = make_build(tmp_path)
    result = report.build_report(report.collect(build, "", 1), 5)
    first = result["headers"][0]
    assert first["header"] == "/inc/Own.h"
    assert first["tus"] == 2
    assert abs(first["seconds"] - 0.0035) < 1e-9
    assert [entry["header"] for entry in result["pch_headers"]][0] == "/inc/pch.h"


def test_templates_are_summed_over_tus(tmp_path):
    build = make_build(tmp_path)
    result = report.build_report(report.collect(build, "", 1), 5)
    first = result["templates"][0]
    assert first["name"] == "std::vector<int>::push_back"
    assert first["tus"] == 2
    assert abs(first["seconds"] - 1.0) < 1e-9


def test_only_restricts_every_list(tmp_path):
    build = make_build(tmp_path)
    result = report.build_report(report.collect(build, "tests/", 1), 5)
    assert result["tus"] == 1
    assert result["pch"] == []
    assert result["cpu_seconds"] == 7.0
    assert [entry["tus"] for entry in result["headers"]] == [1]
    assert abs(result["templates"][0]["seconds"] - 0.3) < 1e-9


def test_main_prints_text_and_json(tmp_path, capsys):
    build = make_build(tmp_path)
    assert report.main(["--build", str(build), "--top", "3", "--jobs", "1"]) == 0
    text = capsys.readouterr().out
    assert "PCH compile: 5.00 s" in text
    assert "Slowest TUs" in text
    assert report.main(["--build", str(build), "--json", "--jobs", "1"]) == 0
    assert json.loads(capsys.readouterr().out)["tus"] == 2


def test_bad_arguments_and_empty_build_are_exit_2(tmp_path, capsys):
    assert report.main(["--build", str(tmp_path / "missing")]) == 2
    empty = tmp_path / "empty"
    empty.mkdir()
    assert report.main(["--build", str(empty), "--jobs", "1"]) == 2
    assert report.main(["--bogus"]) == 2
    capsys.readouterr()
