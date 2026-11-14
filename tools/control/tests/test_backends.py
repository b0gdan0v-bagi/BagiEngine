"""The backend comparison command (bagi_backends.py) against the fake game executable: the `window` profile is the same fake started without --headless."""

import json
from pathlib import Path

import pytest

import bagi_backends
from bagi_backends import main
from bagi_play import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, EXIT_USAGE
from png_reader import PngImage, read_png

import png_ops  # noqa: E402

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
HEADER = {"scenario": 1, "game": "Fake", "flags": ["--headless", "--manual-clock"]}
SHOT = {"method": "screenshot", "params": {"maxSize": 400}, "expect": {"reference": {"png": "references/none.png"}}}


def write(tmp_path: Path, *lines: dict, header: dict | None = None) -> Path:
    path = tmp_path / "pair.jsonl"
    path.write_text("\n".join(json.dumps(line) for line in [HEADER if header is None else header, *lines]) + "\n", encoding="utf-8")
    return path


def serve(monkeypatch, tmp_path, name, left, right):
    path = tmp_path / f"{name}.png"
    path.write_bytes(png_ops.encode_png(PngImage(2, 1, 3, [bytes(left + right)])))
    monkeypatch.setenv(name, str(path))


def run(fake_game_exe, scenario_path, out, *extra):
    return main([str(fake_game_exe), str(scenario_path), "--out", str(out), *extra])


def test_two_profiles_with_equal_pictures_are_unchanged(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, {"method": "step"}, SHOT, SHOT)
    out = tmp_path / "out"
    assert run(fake_game_exe, path, out) == EXIT_PASSED
    output = capsys.readouterr().out
    assert "software (A) vs window (B):" in output and output.count("unchanged") == 2
    assert "summary: 0 of 2 pictures differ" in output
    assert (out / "software" / "1-pair.png").is_file() and (out / "window" / "2-pair.png").is_file()
    assert (out / "diff-window-1.png").is_file()


def test_different_pictures_give_the_share_the_box_and_the_map(fake_game_exe, tmp_path, monkeypatch, capsys):
    serve(monkeypatch, tmp_path, "FAKE_GAME_WINDOW_PICTURE", (255, 0, 0), (0, 0, 9))
    path = write(tmp_path, SHOT)
    out = tmp_path / "out"
    assert run(fake_game_exe, path, out) == EXIT_PASSED  # informs, never gates without --max-share
    output = capsys.readouterr().out
    assert "1: 50% changed (1 of 2 px), box [1, 0, 1, 0]" in output and "summary: 1 of 1 pictures differ, the largest: 50% (window 1)" in output
    assert read_png((out / "diff-window-1.png").read_bytes()).pixel(1, 0) == (255, 0, 0)
    assert read_png((out / "window" / "1-pair.png").read_bytes()).pixel(1, 0) == (0, 0, 9)  # the game's own picture


def test_the_threshold_hides_a_small_difference(fake_game_exe, tmp_path, monkeypatch, capsys):
    serve(monkeypatch, tmp_path, "FAKE_GAME_WINDOW_PICTURE", (255, 0, 5), (0, 0, 255))  # the fake's own picture, the first pixel 5 off in one channel
    path = write(tmp_path, SHOT)
    run(fake_game_exe, path, tmp_path / "out")
    assert "50% changed" in capsys.readouterr().out
    run(fake_game_exe, path, tmp_path / "out", "--threshold", "5")
    assert "unchanged" in capsys.readouterr().out


def test_a_picture_of_another_size_is_scaled_and_said_so(fake_game_exe, tmp_path, monkeypatch, capsys):
    path = tmp_path / "wide.png"
    path.write_bytes(png_ops.encode_png(PngImage(4, 2, 3, [bytes([255, 0, 0] * 2 + [0, 0, 255] * 2)] * 2)))
    monkeypatch.setenv("FAKE_GAME_WINDOW_PICTURE", str(path))
    assert run(fake_game_exe, write(tmp_path, SHOT), tmp_path / "out") == EXIT_PASSED
    assert "unchanged" in capsys.readouterr().out  # the same two colors, B scaled to A's size


def test_a_profile_that_does_not_start_is_named_and_skipped(fake_game_exe, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_GAME_NO_WINDOW", "1")
    assert run(fake_game_exe, write(tmp_path, SHOT), tmp_path / "out") == EXIT_PASSED
    output = capsys.readouterr().out
    assert "window: not available:" in output and "1 backend available: there is nothing to compare" in output
    assert (tmp_path / "out" / "software" / "1-pair.png").is_file()


def test_only_one_requested_profile_says_so_and_exits_zero(fake_game_exe, tmp_path, capsys):
    assert run(fake_game_exe, write(tmp_path, SHOT), tmp_path / "out", "--backends", "software") == EXIT_PASSED
    assert "nothing to compare" in capsys.readouterr().out


def test_vulkan_and_the_browser_are_listed_and_never_started(fake_game_exe, tmp_path, monkeypatch, capsys):
    launches = []
    original = bagi_backends.GameSession.launch
    monkeypatch.setattr(bagi_backends.GameSession, "launch", lambda self, *a, **k: launches.append(k["headless"]) or original(self, *a, **k))
    assert run(fake_game_exe, write(tmp_path, SHOT), tmp_path / "out", "--backends", "software,vulkan,browser") == EXIT_PASSED
    output = capsys.readouterr().out
    assert "vulkan: not available: cannot capture (" in output and "browser: not available: cannot capture (" in output
    assert "Headless and screenshots" in output
    assert launches == [True]


def test_max_share_exceeded_exits_one_and_a_small_share_does_not(fake_game_exe, tmp_path, monkeypatch, capsys):
    serve(monkeypatch, tmp_path, "FAKE_GAME_WINDOW_PICTURE", (255, 0, 0), (0, 0, 9))
    path = write(tmp_path, SHOT)
    assert run(fake_game_exe, path, tmp_path / "out", "--max-share", "0.1") == EXIT_FAILED
    assert "over --max-share" in capsys.readouterr().out
    assert run(fake_game_exe, path, tmp_path / "out", "--max-share", "0.5") == EXIT_PASSED


def test_out_inside_a_work_tree_is_refused(fake_game_exe, tmp_path, capsys):
    assert run(fake_game_exe, write(tmp_path, SHOT), REPOSITORY_ROOT / "tools" / "pictures-never") == EXIT_USAGE
    assert "git work tree" in capsys.readouterr().out
    assert not (REPOSITORY_ROOT / "tools" / "pictures-never").exists()


def test_a_bad_scenario_exits_two(fake_game_exe, tmp_path, capsys):
    broken = tmp_path / "broken.jsonl"
    broken.write_text("{not json\n")
    assert run(fake_game_exe, broken, tmp_path / "out") == EXIT_USAGE
    assert run(fake_game_exe, tmp_path / "missing.jsonl", tmp_path / "out") == EXIT_USAGE
    assert run(fake_game_exe, write(tmp_path, {"method": "get_state"}), tmp_path / "out") == EXIT_USAGE
    assert "no screenshot call" in capsys.readouterr().out


def test_a_bad_argument_exits_two(fake_game_exe, tmp_path):
    path = write(tmp_path, SHOT)
    for extra in (["--backends", "metal"], ["--threshold", "300"], ["--max-share", "2"]):
        with pytest.raises(SystemExit) as refusal:
            run(fake_game_exe, path, tmp_path / "out", *extra)
        assert refusal.value.code == EXIT_USAGE


def test_a_missing_executable_is_skipped(tmp_path, capsys):
    assert main([str(tmp_path / "NoGame"), str(write(tmp_path, SHOT)), "--out", str(tmp_path / "out")]) == EXIT_SKIPPED


def test_the_game_of_the_header_is_checked(fake_game_exe, tmp_path, capsys):
    path = write(tmp_path, SHOT, header={**HEADER, "game": "Other"})
    assert run(fake_game_exe, path, tmp_path / "out") == EXIT_PASSED
    output = capsys.readouterr().out
    assert output.count("not available: the game that started is 'Fake'") == 2
