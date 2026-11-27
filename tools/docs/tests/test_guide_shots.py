"""guide_shots.py against the control tests' fake game: no built game is needed."""

import json
import subprocess
import sys
from pathlib import Path

import guide_shots
import pytest

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def write_manifest(folder, rows):
    path = folder / "shots.json"
    path.write_text(json.dumps({"shots": rows}))
    return path


def run(manifest, out, *arguments):
    return subprocess.run(
        [sys.executable, guide_shots.__file__, "--manifest", str(manifest), "--out", str(out), *arguments],
        capture_output=True,
        text=True,
    )


def row(**fields):
    return {"name": "one", "exe": "FakeGame", **fields}


def test_a_row_writes_its_picture_after_its_steps_in_order(tmp_path, fake_bin, monkeypatch):
    calls = []
    original = guide_shots.GameSession.call

    def recording_call(self, method, params=None, timeout=None):
        calls.append(method)
        return original(self, method, params, timeout)

    monkeypatch.setattr(guide_shots.GameSession, "call", recording_call)
    manifest = write_manifest(tmp_path, [row(steps=[["step", {"frames": 2}], ["ping", {}], ["step", {"frames": 1}]])])
    out = tmp_path / "img"
    code = guide_shots.main(["--manifest", str(manifest), "--out", str(out), "--bin", str(fake_bin)])
    assert code == 0
    steps = calls[calls.index("step") :]
    assert steps == ["step", "ping", "step", "screenshot"]
    assert (out / "one.png").read_bytes().startswith(PNG_SIGNATURE)


def test_only_takes_the_named_rows_and_an_unknown_name_is_an_error(tmp_path, fake_bin):
    manifest = write_manifest(tmp_path, [row(name="one"), row(name="two")])
    out = tmp_path / "img"
    assert run(manifest, out, "--bin", str(fake_bin), "--only", "two").returncode == 0
    assert sorted(path.name for path in out.iterdir()) == ["two.png"]
    result = run(manifest, out, "--bin", str(fake_bin), "--only", "three")
    assert result.returncode == 1
    assert "three" in result.stderr


def test_a_missing_executable_is_exit_77_and_writes_nothing(tmp_path):
    manifest = write_manifest(tmp_path, [row()])
    out = tmp_path / "img"
    result = run(manifest, out, "--bin", str(tmp_path / "nowhere"))
    assert result.returncode == 77
    assert "build the preset first" in result.stderr
    assert not out.exists()


def test_a_failing_step_is_exit_1_and_writes_no_picture(tmp_path, fake_bin):
    manifest = write_manifest(tmp_path, [row(steps=[["no_such_method", {}]])])
    out = tmp_path / "img"
    assert run(manifest, out, "--bin", str(fake_bin)).returncode == 1
    assert not (out / "one.png").exists()


@pytest.mark.parametrize(
    "rows",
    [
        [{"exe": "X"}],
        [{"name": "Bad Name", "exe": "X"}],
        [{"name": "a"}],
        [{"name": "a", "exe": "X", "steps": [["step"]]}],
        [{"name": "a", "exe": "X"}, {"name": "a", "exe": "X"}],
    ],
)
def test_a_bad_manifest_is_exit_1(tmp_path, rows):
    result = run(write_manifest(tmp_path, rows), tmp_path / "img", "--bin", str(tmp_path))
    assert result.returncode == 1
    assert "guide_shots:" in result.stderr


def test_the_shipped_manifest_is_valid():
    rows = guide_shots.load_rows(guide_shots.MANIFEST)
    assert {item["name"] for item in rows} >= {"demo-game", "demo-editor"}


def overlay_tree(tmp_path, monkeypatch):
    """A fake repository root with the folder `examples/one` (a file `config/Marker.txt`) that a row can name."""
    root = tmp_path / "repository"
    (root / "examples" / "one" / "config").mkdir(parents=True)
    (root / "examples" / "one" / "config" / "Marker.txt").write_text("marker")
    monkeypatch.setattr(guide_shots, "REPOSITORY_ROOT", root)
    return root


def test_a_content_overlay_is_a_copy_the_game_may_write_into_and_it_is_removed_after(tmp_path, fake_bin, monkeypatch):
    root = overlay_tree(tmp_path, monkeypatch)
    seen = {}
    launch = guide_shots.GameSession.launch
    call = guide_shots.GameSession.call

    def recording_launch(self, *args, **kwargs):
        seen["overlay"] = kwargs.get("content_overlay")
        return launch(self, *args, **kwargs)

    def writing_call(self, method, params=None, timeout=None):
        if method == "screenshot":
            copy = Path(seen["overlay"])
            seen["marker"] = (copy / "config" / "Marker.txt").read_text()
            (copy / "config" / "Written.txt").write_text("by the game")
        return call(self, method, params, timeout)

    monkeypatch.setattr(guide_shots.GameSession, "launch", recording_launch)
    monkeypatch.setattr(guide_shots.GameSession, "call", writing_call)
    manifest = write_manifest(tmp_path, [row(contentOverlay="examples/one")])
    assert guide_shots.main(["--manifest", str(manifest), "--out", str(tmp_path / "img"), "--bin", str(fake_bin)]) == 0
    assert seen["marker"] == "marker"
    assert Path(seen["overlay"]) != root / "examples" / "one"
    assert not Path(seen["overlay"]).exists()
    assert sorted(path.name for path in (root / "examples" / "one" / "config").iterdir()) == ["Marker.txt"]


def test_a_row_without_a_content_overlay_passes_none(tmp_path, fake_bin, monkeypatch):
    seen = []
    launch = guide_shots.GameSession.launch

    def recording_launch(self, *args, **kwargs):
        seen.append(kwargs.get("content_overlay", "absent"))
        return launch(self, *args, **kwargs)

    monkeypatch.setattr(guide_shots.GameSession, "launch", recording_launch)
    manifest = write_manifest(tmp_path, [row()])
    assert guide_shots.main(["--manifest", str(manifest), "--out", str(tmp_path / "img"), "--bin", str(fake_bin)]) == 0
    assert seen == [None]


@pytest.mark.parametrize("overlay", ["examples/missing", "examples/one/config/Marker.txt"])
def test_a_content_overlay_that_is_no_folder_is_exit_1_naming_the_row(tmp_path, fake_bin, monkeypatch, capsys, overlay):
    overlay_tree(tmp_path, monkeypatch)
    manifest = write_manifest(tmp_path, [row(name="shot-row", contentOverlay=overlay)])
    assert guide_shots.main(["--manifest", str(manifest), "--out", str(tmp_path / "img"), "--bin", str(fake_bin)]) == 1
    assert "shot-row" in capsys.readouterr().err
    assert not (tmp_path / "img").exists()


@pytest.mark.parametrize("overlay", ["../outside", "examples/../../outside", "/etc", "C:/temp", "..", 7, ""])
def test_a_content_overlay_that_leaves_the_repository_is_exit_1_naming_the_row(tmp_path, monkeypatch, capsys, overlay):
    overlay_tree(tmp_path, monkeypatch)
    manifest = write_manifest(tmp_path, [row(name="shot-row", contentOverlay=overlay)])
    assert guide_shots.main(["--manifest", str(manifest), "--out", str(tmp_path / "img"), "--bin", str(tmp_path)]) == 1
    assert "shot-row" in capsys.readouterr().err
