"""The run script of the libFuzzer executables: the command line, the exit codes and the folders, over fake executables (no fuzzer needed)."""

import subprocess
import sys
from pathlib import Path

import pytest

FUZZ_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FUZZ_DIR))

import run_fuzzers  # noqa: E402

TARGETS_CPP = """
    static const FuzzTarget targets[] = {
        {"BinaryDeserializer", &FuzzBinaryDeserializer, "binary", 1},
        {"XmlDeserializer", &FuzzXmlDeserializer, "xml", 2},
    };
"""


@pytest.fixture
def layout(tmp_path):
    """A fake repository (the target list and two corpus folders) and a fake build folder."""
    root = tmp_path / "repo"
    (root / "tests" / "Fuzz" / "corpus" / "binary").mkdir(parents=True)
    (root / "tests" / "Fuzz" / "corpus" / "xml").mkdir(parents=True)
    (root / "tests" / "Fuzz" / "corpus" / "binary" / "seed.bin").write_bytes(b"BEBN")
    (root / "tests" / "Fuzz" / "FuzzTargets.cpp").write_text(TARGETS_CPP, encoding="utf-8")
    build = tmp_path / "build" / "fuzz"
    (build / "bin").mkdir(parents=True)
    return root, build


def make_executable(build, name):
    path = build / "bin" / f"{run_fuzzers.EXECUTABLE_PREFIX}{name}"
    path.write_text("", encoding="utf-8")
    return path


def fake_runner(returncode=0, artifact=None):
    """A runner that records the command; with `artifact` it leaves that file in the -artifact_prefix folder like a crashing fuzzer."""
    calls = []

    def runner(command, stdout=None, stderr=None, check=False):
        calls.append(command)
        if artifact is not None:
            prefix = next(argument for argument in command if argument.startswith("-artifact_prefix="))
            Path(prefix.split("=", 1)[1] + artifact).write_bytes(b"x")
        return subprocess.CompletedProcess(command, returncode)

    runner.calls = calls
    return runner


def run(layout, runner, *extra):
    root, build = layout
    return run_fuzzers.main(["--build-dir", str(build), "--seconds", "7", "--root", str(root), *extra], runner=runner)


def test_the_target_table_is_read_from_the_cpp_list(layout):
    root, _ = layout
    assert run_fuzzers.read_target_table(root) == {"BinaryDeserializer": "binary", "XmlDeserializer": "xml"}


def test_the_real_target_list_has_a_row_for_every_corpus_folder_in_the_repository():
    table = run_fuzzers.read_target_table(run_fuzzers.repository_root())
    assert table["BinaryDeserializer"] == "binary"
    corpus = run_fuzzers.repository_root() / "tests" / "Fuzz" / "corpus"
    assert sorted(path.name for path in corpus.iterdir() if path.is_dir()) == sorted(table.values())


def test_the_command_line_has_the_limits_and_the_two_corpus_folders():
    command = run_fuzzers.build_command(Path("bin/BagiFuzzX"), 30, Path("work/X"), Path("corpus/x"), Path("art/X-"))
    assert command == [
        "bin/BagiFuzzX", "-max_total_time=30", "-max_len=65536", "-artifact_prefix=art/X-", "-rss_limit_mb=2048", "-timeout=10",
        "work/X", "corpus/x",
    ]


def test_executables_are_found_by_prefix_and_a_dsym_folder_is_not_one(layout):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    (build / "bin" / "BagiFuzzBinaryDeserializer.dSYM").mkdir()
    (build / "bin" / "BagiEngineTests").write_text("", encoding="utf-8")
    assert list(run_fuzzers.find_executables(build)) == ["BinaryDeserializer"]


def test_no_executable_is_a_skip_with_the_77_code(layout, capsys):
    assert run(layout, fake_runner()) == run_fuzzers.EXIT_SKIPPED
    assert "cmake --preset macos-fuzz" in capsys.readouterr().out


def test_a_clean_run_exits_0_and_runs_over_a_copy_of_the_corpus(layout, capsys):
    root, build = layout
    make_executable(build, "BinaryDeserializer")
    runner = fake_runner()
    assert run(layout, runner) == 0
    command = runner.calls[0]
    assert "-max_total_time=7" in command
    assert command[-2] == str(build / "fuzz-work" / "BinaryDeserializer")
    assert command[-1] == str(root / "tests" / "Fuzz" / "corpus" / "binary")
    assert (build / "fuzz-work" / "BinaryDeserializer" / "seed.bin").read_bytes() == b"BEBN"  # the copy, so the repository never gets new inputs
    assert "BinaryDeserializer: ok" in capsys.readouterr().out


def test_the_work_folder_starts_fresh_every_time(layout):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    stale = build / "fuzz-work" / "BinaryDeserializer" / "old"
    stale.parent.mkdir(parents=True)
    stale.write_bytes(b"old")
    run(layout, fake_runner())
    assert not stale.exists()


def test_an_artifact_is_a_finding_with_exit_1_and_the_hint(layout, capsys):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    assert run(layout, fake_runner(returncode=77, artifact="crash-abc")) == run_fuzzers.EXIT_FINDING
    out = capsys.readouterr().out
    assert "FINDING" in out and "crash-abc" in out
    assert "-minimize_crash=1" in out and "tests/Fuzz/corpus/binary/fix-<what>" in out
    assert (build / "fuzz-artifacts" / "BinaryDeserializer-crash-abc").exists()


def test_an_old_artifact_is_not_a_new_finding(layout):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    (build / "fuzz-artifacts").mkdir()
    (build / "fuzz-artifacts" / "BinaryDeserializer-crash-old").write_bytes(b"x")
    assert run(layout, fake_runner()) == 0


def test_a_failing_fuzzer_without_an_artifact_is_still_a_failure(layout, capsys):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    assert run(layout, fake_runner(returncode=1)) == run_fuzzers.EXIT_FINDING
    assert "FAILED, exit code 1" in capsys.readouterr().out


def test_target_selects_executables_and_an_unknown_one_is_an_error(layout):
    _, build = layout
    make_executable(build, "BinaryDeserializer")
    make_executable(build, "XmlDeserializer")
    runner = fake_runner()
    assert run(layout, runner, "--target", "XmlDeserializer") == 0
    assert [Path(call[0]).name for call in runner.calls] == ["BagiFuzzXmlDeserializer"]
    assert run(layout, fake_runner(), "--target", "Nope") == run_fuzzers.EXIT_FINDING


def test_an_executable_missing_from_the_target_list_is_reported(layout, capsys):
    _, build = layout
    make_executable(build, "Orphan")
    assert run(layout, fake_runner()) == run_fuzzers.EXIT_FINDING
    assert "not in FuzzTargetList()" in capsys.readouterr().out


@pytest.mark.skipif(sys.platform == "win32", reason="the fake executable is a shell script")
def test_a_whole_run_over_a_shell_script_that_writes_an_artifact(layout):
    _, build = layout
    script = make_executable(build, "BinaryDeserializer")
    script.write_text('#!/bin/sh\nfor a in "$@"; do case "$a" in -artifact_prefix=*) : > "${a#-artifact_prefix=}crash-x";; esac; done\nexit 77\n', encoding="utf-8")
    script.chmod(0o755)
    assert run(layout, subprocess.run) == run_fuzzers.EXIT_FINDING
    assert (build / "fuzz-artifacts" / "BinaryDeserializer-crash-x").exists()
