"""editor_frames.py: the numbers (pure), the sequence over a stub session that records its calls, and the skip and failure exits. The real editor is
checked by hand and by the benchmark `editor-frame` of the set."""

import argparse
import subprocess
import sys
from pathlib import Path

import pytest

PERF_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PERF_DIR))

import editor_frames  # noqa: E402
from bagi_session import SessionError  # noqa: E402


def frame(cpu: float, present: float = 0.0, wait: float = 0.0, draw: float = 0.0, allocs: float = 0.0, size: float = 0.0) -> dict:
    return {"cpuMs": cpu, "stagesMs": {"Update": cpu - present - wait, "Present": present, "Wait": wait}, "drawCalls": draw, "allocCount": allocs, "allocBytes": size}


def test_percentiles_are_nearest_rank_and_one_frame_is_every_percentile():
    series = [frame(cpu) for cpu in range(1, 101)]
    numbers = editor_frames.summarize(series)
    assert numbers["cpuMsP50"] == 50 and numbers["cpuMsP99"] == 99
    assert numbers["workMsP50"] == 50 and numbers["workMsP99"] == 99
    odd = editor_frames.summarize([frame(cpu) for cpu in (10, 30, 20)])
    assert odd["cpuMsP50"] == 20 and odd["cpuMsP99"] == 30  # ceil(0.5 * 3) = 2nd, ceil(0.99 * 3) = 3rd of the sorted
    single = editor_frames.summarize([frame(7.0)])
    assert single["workMsP50"] == single["workMsP99"] == single["cpuMsP50"] == single["cpuMsP99"] == 7.0


def test_the_present_and_the_wait_are_not_work_and_work_is_never_negative():
    numbers = editor_frames.summarize([frame(10.0, present=6.0, wait=1.0)])
    assert numbers["workMsP50"] == pytest.approx(3.0) and numbers["cpuMsP50"] == 10.0
    assert editor_frames.work_ms(frame(2.0, present=3.0)) == 0.0
    assert editor_frames.work_ms({"cpuMs": 4.0}) == 4.0  # a frame with no stage list


def test_the_counters_are_means():
    numbers = editor_frames.summarize([frame(1, draw=10, allocs=0, size=0), frame(1, draw=20, allocs=4, size=100)])
    assert (numbers["drawCalls"], numbers["allocsPerFrame"], numbers["allocBytesPerFrame"]) == (15.0, 2.0, 50.0)
    assert editor_frames.summarize([])["workMsP50"] == 0.0


class StubSession:
    """Records the calls of one measure(); `frame_stats` answers as many frames as asked, 1 ms each."""

    def __init__(self, log: list):
        self.log = log
        self.steps: list[int] = []

    def call(self, method, params=None):
        self.log.append((method, params))
        if method == "frame_stats":
            return {"frames": [frame(1.0)] * params["frames"], "allocTracking": True}
        return {}

    def step(self, frames=1, dt=None):
        self.log.append(("step", frames))

    def imgui_input(self, events):
        self.log.append(("imgui_input", events))


def args_of(**given) -> argparse.Namespace:
    values = {"frames": 300, "warmup": 120, "count": 500, "chunk": 240, "trace": None, "exe": "x"}
    values.update(given)
    return argparse.Namespace(**values)


@pytest.fixture
def moves(monkeypatch):
    """The moves of bagi_qa_editor are recorded into one list with the session's calls, so the order of the whole sequence shows."""
    log: list = []
    for name in ("widen_left_dock", "open_panel", "select_node", "open_component"):
        monkeypatch.setattr(editor_frames.editor, name, lambda session, *rest, _name=name: log.append((_name, *rest)))
    monkeypatch.setattr(editor_frames.editor, "panel_open", lambda session, title: True)
    return log


def test_the_sequence_opens_the_panels_then_warms_up_then_measures_in_chunks(moves):
    result = editor_frames.measure(StubSession(moves), args_of())
    assert [entry[0] for entry in moves][:10] == ["reset", "step", "widen_left_dock", "open_panel", "select_node", "open_component", "open_panel", "open_panel", "imgui_input", "step"]
    assert moves[0] == ("reset", {"scene": "sprites", "count": 500})
    assert [entry[1] for entry in moves if entry[0] == "open_panel"] == ["Scene Editor", "Viewport", "Game Panel"]
    assert ("select_node", "Run") in moves and ("open_component", "TransformComponent") in moves
    assert moves[8][1] == [{"type": "move", "x": -10.0, "y": -10.0}]
    steps = [entry[1] for entry in moves if entry[0] == "step"]
    assert steps == [3, 3, 120, 240, 60]  # settle, settle after the pointer, warm-up, then 240 + 60 measured
    stats = [entry[1]["frames"] for entry in moves if entry[0] == "frame_stats"]
    assert stats == [240, 60]  # none for the warm-up, exactly the chunk each
    assert moves.index(("frame_stats", {"frames": 240})) > moves.index(("step", 120))
    assert result["frames"] == 300 and result["warmup"] == 120 and result["scene"] == "sprites" and result["count"] == 500
    assert result["allocTracking"] is True and result["panels"] == ["Scene Editor", "Viewport", "Game Panel"] and result["workMsP50"] == 1.0


def test_a_panel_that_is_not_drawn_at_the_end_is_an_error(moves, monkeypatch):
    monkeypatch.setattr(editor_frames.editor, "panel_open", lambda session, title: title != "Game Panel")
    with pytest.raises(SessionError, match="Game Panel"):
        editor_frames.measure(StubSession(moves), args_of())


def test_no_warmup_means_no_warmup_step(moves):
    editor_frames.measure(StubSession(moves), args_of(warmup=0, frames=10))
    assert [entry[1] for entry in moves if entry[0] == "step"] == [3, 3, 10]


class GameModeSession(StubSession):
    """A launch that answers Game: the build has no editor."""

    closed = False

    def launch(self, **_):
        return {"status": {"runMode": "Game"}}

    def quit(self):
        return {}

    def close(self):
        GameModeSession.closed = True


def test_a_build_without_the_editor_is_a_skip_and_the_game_is_closed(monkeypatch, tmp_path, capsys):
    exe = tmp_path / "Game"
    exe.write_text("", encoding="utf-8")
    monkeypatch.setattr(editor_frames, "GameSession", lambda: GameModeSession([]))
    assert editor_frames.run(args_of(exe=str(exe))) == editor_frames.EXIT_SKIPPED
    output = capsys.readouterr().out
    assert output.startswith("skip: ") and "Game" in output and GameModeSession.closed


def test_a_missing_executable_is_a_skip_in_the_script(tmp_path):
    done = subprocess.run([sys.executable, str(PERF_DIR / "editor_frames.py"), "--exe", str(tmp_path / "none")], capture_output=True, text=True)
    assert done.returncode == 77 and done.stdout.startswith("skip: ")


class EditorSession(GameModeSession):
    def launch(self, **_):
        return {"status": {"runMode": "Editor"}}


def test_a_move_that_fails_is_an_error_not_a_skip(monkeypatch, tmp_path, capsys, moves):
    exe = tmp_path / "Game"
    exe.write_text("", encoding="utf-8")

    def renamed(session, name):
        raise SessionError("0 hierarchy rows are called 'Run'")

    monkeypatch.setattr(editor_frames.editor, "select_node", renamed)
    monkeypatch.setattr(editor_frames, "GameSession", lambda: EditorSession(moves))
    assert editor_frames.run(args_of(exe=str(exe))) == editor_frames.EXIT_FAILED
    captured = capsys.readouterr()
    assert captured.out == "" and "hierarchy rows" in captured.err


def test_a_good_run_prints_one_json_line_and_restores_the_editor_folder(monkeypatch, tmp_path, capsys, moves):
    exe = tmp_path / "Game"
    exe.write_text("", encoding="utf-8")
    seen = {}

    class Recording(EditorSession):
        def launch(self, **keywords):
            seen["folder"] = editor_frames.os.environ.get(editor_frames.EDITOR_DIR_VARIABLE)
            seen.update(keywords)
            return super().launch()

    monkeypatch.delenv(editor_frames.EDITOR_DIR_VARIABLE, raising=False)
    monkeypatch.setattr(editor_frames, "GameSession", lambda: Recording(moves))
    assert editor_frames.run(args_of(exe=str(exe), trace=str(tmp_path / "t.json"))) == 0
    line = capsys.readouterr().out.strip().splitlines()
    assert len(line) == 1 and line[0].startswith("{") and '"workMsP50"' in line[0]
    assert seen["editor"] is True and seen["window"] == "1920x1080"
    assert seen["extra"] == ["--stress-scene=sprites", "--stress-count=500", f"--trace={(tmp_path / 't.json').resolve()}"]
    assert seen["folder"] and not Path(seen["folder"]).exists()  # a temporary folder, gone at the end
    assert editor_frames.EDITOR_DIR_VARIABLE not in editor_frames.os.environ
