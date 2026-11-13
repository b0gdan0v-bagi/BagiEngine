"""stress_frames.py: the pure parts (the flags, the choice of the line, the step bound) and the whole script against a fake BagiStress
(`fake_stress_game.py`: it quits in the middle of a step and prints its line, as the real game does; the real one is checked by hand and by
the benchmarks of the set)."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

import pytest

PERF_DIR = Path(__file__).resolve().parents[1]
FAKE_GAME = Path(__file__).resolve().parent / "fake_stress_game.py"
sys.path.insert(0, str(PERF_DIR))

import stress_frames  # noqa: E402


def make_args(*argv: str) -> argparse.Namespace:
    """The namespace main() would parse: the parser is rebuilt from the module's own defaults."""
    namespace = argparse.Namespace(
        exe="BagiStress",
        scene="sprites",
        count=stress_frames.DEFAULT_COUNT,
        frames=stress_frames.DEFAULT_FRAMES,
        warmup=stress_frames.DEFAULT_WARMUP,
        ramp=False,
        measure="work",
        interleave=False,
        atlases=stress_frames.DEFAULT_ATLASES,
        chunk=stress_frames.DEFAULT_CHUNK,
        timeout=stress_frames.DEFAULT_TIMEOUT_SECONDS,
        trace=None,
    )
    for item in argv:
        name, _, value = item.partition("=")
        key = name.lstrip("-").replace("-", "_")
        current = getattr(namespace, key)
        setattr(namespace, key, True if value == "" and isinstance(current, bool) else type(current)(value) if current is not None else value)
    return namespace


def test_a_fixed_run_passes_the_count_the_frames_and_the_warmup():
    flags = stress_frames.stress_flags(make_args("--scene=ui", "--count=300", "--frames=600", "--warmup=30"))
    assert flags == ["--stress-scene=ui", "--stress-measure=work", "--stress-atlases=4", "--stress-count=300", "--stress-frames=600", "--stress-warmup=30"]


def test_a_ramp_passes_neither_the_count_nor_the_frames():
    flags = stress_frames.stress_flags(make_args("--ramp", "--measure=cpu", "--interleave", "--count=7"))
    assert "--stress-ramp" in flags and "--stress-interleave" in flags and "--stress-measure=cpu" in flags
    assert not any(flag.startswith(("--stress-count", "--stress-frames", "--stress-warmup")) for flag in flags)


def test_a_trace_is_an_absolute_flag_of_the_game(tmp_path):
    flags = stress_frames.stress_flags(make_args(f"--trace={tmp_path / 'trace.json'}"))
    assert f"--trace={(tmp_path / 'trace.json').resolve()}" in flags


def test_the_step_bound_has_slack_over_the_run_of_the_game():
    assert stress_frames.step_bound(make_args("--frames=600", "--warmup=30")) == 630 + stress_frames.STEP_SLACK_FRAMES
    assert stress_frames.step_bound(make_args("--ramp")) == stress_frames.MAX_RAMP_FRAMES + stress_frames.STEP_SLACK_FRAMES


def test_the_last_line_that_starts_with_a_brace_is_the_line():
    log = '[Info] [12:00:00.000] start\n{"first": 1}\n[Info] [12:00:00.001] between\n{"second": 2}\n[Info] [12:00:00.002] after\n'
    assert stress_frames.last_json_line(log) == '{"second": 2}'


def test_a_log_without_a_brace_line_has_none():
    assert stress_frames.last_json_line("[Info] [12:00:00.000] start\n  {indented}\n") is None
    assert stress_frames.last_json_line("") is None


@pytest.fixture
def fake_stress_exe(tmp_path):
    """An executable that runs fake_stress_game.py with the flags it is given (a script with a shebang, a .bat on Windows)."""
    if sys.platform == "win32":
        exe = tmp_path / "FakeStress.bat"
        exe.write_text(f'@"{sys.executable}" "{FAKE_GAME}" %*\r\n@exit /b %ERRORLEVEL%\r\n')
        return exe
    exe = tmp_path / "FakeStress"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    return exe


def run_script(exe: Path, *extra: str, env: dict | None = None) -> subprocess.CompletedProcess:
    import os

    return subprocess.run(
        [sys.executable, str(PERF_DIR / "stress_frames.py"), "--exe", str(exe), *extra],
        capture_output=True,
        text=True,
        timeout=60,
        env={**os.environ, **(env or {})},
    )


def test_a_fixed_run_prints_the_games_line_and_nothing_else(fake_stress_exe):
    result = run_script(fake_stress_exe, "--scene", "entities", "--count", "50", "--frames", "100", "--warmup", "20", "--chunk", "30")
    assert result.returncode == 0, result.stderr
    assert result.stdout.count("\n") == 1
    line = json.loads(result.stdout)
    assert line["mode"] == "fixed"
    assert "--stress-scene=entities" in line["flags"] and "--stress-count=50" in line["flags"] and "--stress-frames=100" in line["flags"]


def test_a_ramp_prints_the_ramp_line(fake_stress_exe):
    result = run_script(fake_stress_exe, "--scene", "ui", "--ramp", "--measure", "cpu")
    assert result.returncode == 0, result.stderr
    line = json.loads(result.stdout)
    assert line["mode"] == "ramp" and "--stress-ramp" in line["flags"] and "--stress-measure=cpu" in line["flags"]


def test_a_game_that_quits_without_a_line_is_an_error_with_its_log(fake_stress_exe):
    result = run_script(fake_stress_exe, "--frames", "50", "--warmup", "0", env={"FAKE_STRESS_MODE": "noline"})
    assert result.returncode == 1 and result.stdout == ""
    assert "without a JSON line" in result.stderr and "the run is over" in result.stderr


def test_a_non_zero_exit_code_is_an_error_even_with_a_line(fake_stress_exe):
    result = run_script(fake_stress_exe, "--frames", "50", "--warmup", "0", env={"FAKE_STRESS_MODE": "crash"})
    assert result.returncode == 1 and result.stdout == ""
    assert "exit code 4" in result.stderr


def test_a_line_that_is_not_json_is_an_error(fake_stress_exe):
    result = run_script(fake_stress_exe, "--frames", "50", "--warmup", "0", env={"FAKE_STRESS_MODE": "torn"})
    assert result.returncode == 1 and result.stdout == "" and "does not parse" in result.stderr


def test_a_game_that_never_quits_ends_at_the_step_bound(fake_stress_exe):
    result = run_script(fake_stress_exe, "--frames", "10", "--warmup", "0", "--chunk", "400", env={"FAKE_STRESS_MODE": "never"})
    assert result.returncode == 1 and result.stdout == ""
    assert "did not quit in 800 stepped frames" in result.stderr
