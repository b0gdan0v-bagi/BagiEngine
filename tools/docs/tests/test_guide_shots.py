"""guide_shots.py against the control tests' fake game: no built game is needed."""

import json
import subprocess
import sys

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
