"""A short charter on the real BagiStress through QaSession: the library, the invariants module and the game agree.

Skipped when BagiStress is not built (the build of any preset counts), as the control smoke is.
"""

from pathlib import Path

import pytest

from bagi_qa import QaSession
from game_process import REPOSITORY_ROOT, preset_executable

INVARIANTS = Path(__file__).resolve().parents[2] / "stress" / "qa" / "invariants.py"
GAME = "BagiStress"
SCENES = ["sprites", "entities", "ui"]
SWEEP = [1, 7, 500, 2000]
SMALL_COUNT = 40  # a debug build lays out every label of the ui scene: a small count keeps the run short


def built_executable():
    builds = REPOSITORY_ROOT / "build"
    candidates = [preset_executable(preset.name, GAME) for preset in sorted(builds.iterdir())] if builds.is_dir() else []
    existing = [path for path in candidates if path.is_file()]
    return max(existing, key=lambda path: path.stat().st_mtime) if existing else None


@pytest.fixture(scope="module")
def stress_exe():
    exe = built_executable()
    if exe is None:
        pytest.skip(f"{GAME} is not built")
    return exe


def test_a_charter_over_every_scene_and_language_finds_nothing(stress_exe, tmp_path):
    session = QaSession(tmp_path / "out", "every scene, a count sweep and both languages", invariants=INVARIANTS)
    session.launch(exe=str(stress_exe))
    session.reset(scene="sprites", count=SMALL_COUNT)
    for scene in SCENES:
        assert session.act({"type": "set_scene", "scene": scene}).accepted
        session.step(5)
        for language in ("ru", "en"):
            assert session.act({"type": "set_language", "language": language}).accepted
            session.step(3)
        session.shot(f"{scene}-count-{SMALL_COUNT}")
        session.note(f"scene {scene}")
    for count in SWEEP:
        assert session.act({"type": "set_count", "count": count}).accepted
        session.step(2)
    assert session.act({"type": "set_count", "count": SWEEP[-1]}).accepted  # accepted, changes nothing
    assert not session.act({"type": "set_count", "count": session.state()["state"]["maxCount"] + 1}, expect_refusal=True).accepted
    assert not session.act({"type": "set_scene", "scene": "nowhere"}, expect_refusal=True).accepted
    session.reset()
    session.step(2)
    session.finish()
    assert [(finding.name, finding.message) for finding in session.findings] == []
    assert len(session.shots) == len(SCENES) and all(path.stat().st_size > 0 for _, path in session.shots)
