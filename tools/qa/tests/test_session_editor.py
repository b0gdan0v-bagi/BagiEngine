import os
import subprocess

import pytest

import bagi_qa
import bagi_qa_checks as checks
import bagi_qa_repro as repro
import bagi_qa_scene_file as scene_file
import scenario
from bagi_qa import NoEditorError, QaSession
from bagi_session import SessionError

ORIGINAL = "<SceneManager>original</SceneManager>\n"
SAVED = "<SceneManager>saved by the editor</SceneManager>\n"
INC = {"type": "inc"}


@pytest.fixture
def content_file(tmp_path, monkeypatch):
    """A content file the fake editor's Save overwrites, with the fake set up as an editor build."""
    file = tmp_path / "root" / "config" / "SceneConfig.xml"
    file.parent.mkdir(parents=True)
    file.write_text(ORIGINAL, encoding="utf-8")
    monkeypatch.setenv("QA_FAKE_EDITOR", "1")
    monkeypatch.setenv("QA_FAKE_CONTENT_FILE", str(file))
    monkeypatch.setenv("QA_FAKE_SAVE_TEXT", SAVED)
    return file


def overlay_file(session):
    return session.content_overlay / "config" / "SceneConfig.xml"


def start(exe, out_dir, file, **kwargs):
    session = QaSession(out_dir, "an editor charter")
    session.launch(exe=str(exe), editor=True, content_files=[file], **kwargs)
    return session


def release_calls(monkeypatch):
    calls = []
    original = scene_file.ContentGuard.release

    def counting(self):
        restored = original(self)
        calls.append((self, restored))
        return restored

    monkeypatch.setattr(scene_file.ContentGuard, "release", counting)
    return calls


def test_the_editor_gets_a_folder_of_the_session_and_the_scripts_environment_is_unchanged(qa_game_exe, out_dir, content_file, tmp_path, monkeypatch):
    seen = tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_EDITOR_DIR_OUT", str(seen))
    monkeypatch.delenv("BAGI_EDITOR_DIR", raising=False)
    session = start(qa_game_exe, out_dir, content_file)
    assert "BAGI_EDITOR_DIR" not in os.environ
    session.finish()
    assert seen.read_text().strip() == str(session.out_dir / "editor-dir")
    monkeypatch.setenv("BAGI_EDITOR_DIR", "mine")
    start(qa_game_exe, tmp_path / "second", content_file).finish()
    assert os.environ["BAGI_EDITOR_DIR"] == "mine"


def test_a_start_without_the_editor_changes_nothing(qa_game_exe, out_dir, content_file, tmp_path, monkeypatch):
    seen = tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_EDITOR_DIR_OUT", str(seen))
    monkeypatch.delenv("BAGI_EDITOR_DIR", raising=False)
    session = QaSession(out_dir, "a plain charter")
    session.launch(exe=str(qa_game_exe), content_files=[content_file])
    session.imgui_click("Save", window="Scene Editor")
    finished = session.finish()
    assert seen.read_text().strip() == "" and "--editor" not in session.flags
    assert content_file.read_text() == SAVED and finished.restored == []


def test_the_editor_flag_of_a_replayed_header_is_the_same_start(qa_game_exe, out_dir, content_file):
    session = QaSession(out_dir, "a charter")
    session.launch(exe=str(qa_game_exe), extra=["--editor", "--extra"], content_files=[content_file])
    session.finish()
    assert session.flags == ["--headless", "--manual-clock", "--editor", "--extra"]
    assert scenario.load(session.scenario_path)[0].flags.count("--editor") == 1


def test_a_game_without_the_editor_is_stopped_and_the_guard_released(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.delenv("QA_FAKE_EDITOR")
    released = release_calls(monkeypatch)
    session = QaSession(out_dir, "a charter")
    with pytest.raises(NoEditorError, match="status.runMode is Game"):
        session.launch(exe=str(qa_game_exe), editor=True, content_files=[content_file])
    assert isinstance(NoEditorError("x"), SessionError)
    assert len(released) == 1 and released[0][1] == []
    assert not session._game.is_connected and content_file.read_text() == ORIGINAL
    monkeypatch.setenv("QA_FAKE_EDITOR", "1")
    session.launch(exe=str(qa_game_exe), editor=True, content_files=[content_file])  # the session may try again
    session.finish()


def test_a_failed_start_releases_the_guard(tmp_path, out_dir, content_file, monkeypatch):
    released = release_calls(monkeypatch)
    session = QaSession(out_dir, "a charter")
    with pytest.raises(SessionError):
        session.launch(exe=str(tmp_path / "no-such-game"), editor=True, content_files=[content_file])
    assert len(released) == 1 and session.session_errors


def test_save_writes_the_overlay_and_leaves_the_checkout_alone(qa_game_exe, out_dir, content_file):
    session = start(qa_game_exe, out_dir, content_file)
    assert session.content_overlay == session.out_dir / "content-overlay"
    session.imgui_click("Save", window="Scene Editor")
    assert overlay_file(session).read_text() == SAVED and content_file.read_text() == ORIGINAL
    finished = session.finish()
    assert content_file.read_text() == ORIGINAL and overlay_file(session).read_text() == SAVED
    assert finished.restored == [] and session.findings == []
    assert session.finish() is finished


def test_the_game_gets_the_overlay_flag_and_the_recordings_header_lacks_it(qa_game_exe, out_dir, content_file, tmp_path, monkeypatch):
    args = tmp_path / "args.txt"
    monkeypatch.setenv("QA_FAKE_ARGS_OUT", str(args))
    session = start(qa_game_exe, out_dir, content_file)
    session.finish()
    assert f"--content-overlay={session.content_overlay}" in args.read_text().split()
    assert session.content_overlay.parent == session.out_dir
    assert not any("overlay" in flag for flag in session.flags)
    assert not any("overlay" in flag for flag in scenario.load(session.scenario_path)[0].flags)


def test_a_game_run_gets_no_overlay(qa_game_exe, out_dir, content_file, tmp_path, monkeypatch):
    args = tmp_path / "args.txt"
    monkeypatch.setenv("QA_FAKE_ARGS_OUT", str(args))
    session = QaSession(out_dir, "a plain charter")
    session.launch(exe=str(qa_game_exe))
    session.finish()
    assert session.content_overlay is None and "overlay" not in args.read_text()
    with pytest.raises(SessionError, match="content_overlay is for an editor launch"):
        QaSession(tmp_path / "other", "a charter").launch(exe=str(qa_game_exe), content_overlay=tmp_path / "ov")


def test_a_second_session_on_the_same_overlay_starts_on_what_the_first_saved(qa_game_exe, out_dir, content_file, tmp_path, monkeypatch):
    seen = tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_OVERLAY_SEEN_OUT", str(seen))
    first = start(qa_game_exe, out_dir, content_file)
    assert seen.read_text().strip() == "no"
    first.imgui_click("Save", window="Scene Editor")
    first.finish()
    second = start(qa_game_exe, tmp_path / "second", content_file, content_overlay=first.content_overlay)
    assert seen.read_text().strip() == "yes" and second.content_overlay == first.content_overlay
    second.finish()
    third = start(qa_game_exe, tmp_path / "third", content_file)
    assert seen.read_text().strip() == "no"
    third.finish()


def test_editor_state_is_recorded_counted_and_names_the_overlays_file(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.setenv("QA_FAKE_DIRTY", "1")
    session = start(qa_game_exe, out_dir, content_file)
    before = session.calls
    state = session.editor_state()
    assert session.calls == before + 1 and state["anyDirty"]
    assert state["documents"][0]["file"] == str(overlay_file(session))
    session.imgui_click("Save", window="Scene Editor")
    assert not session.editor_state()["anyDirty"]
    session.finish()
    steps = scenario.load(session.scenario_path)[1]
    assert [step.method for step in steps].count("editor_state") == 2 and len(steps) == session.calls


def test_a_game_that_writes_the_checkout_anyway_is_the_finding_and_the_file_is_put_back(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    session = start(qa_game_exe, out_dir, content_file)
    session.imgui_click("Save", window="Scene Editor")
    assert content_file.read_text() == SAVED
    finished = session.finish()
    (finding,) = session.findings
    assert finding.name == "editor.content_file_written" and "overlay" in finding.message and str(session.content_overlay) in finding.message
    assert content_file.read_text() == ORIGINAL and finished.restored == [content_file.resolve()]


def test_content_writes_expected_is_ignored(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    session = start(qa_game_exe, out_dir, content_file)
    session.imgui_click("Save", window="Scene Editor")
    session.finish(content_writes_expected=True)
    assert [finding.name for finding in session.findings] == ["editor.content_file_written"]


def test_a_lost_game_still_gets_the_files_back(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    monkeypatch.setenv("QA_FAKE_CRASH_AT", "1")
    session = start(qa_game_exe, out_dir, content_file)
    session.imgui_click("Save", window="Scene Editor")
    with pytest.raises(SessionError):
        session.act(INC)
    assert content_file.read_text() == SAVED
    session.finish()
    assert content_file.read_text() == ORIGINAL


def test_a_file_the_session_created_is_removed(qa_game_exe, out_dir, content_file, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    content_file.unlink()
    session = start(qa_game_exe, out_dir, content_file)
    session.imgui_click("Save", window="Scene Editor")
    session.finish()
    assert not content_file.exists()


def test_a_click_on_nothing_or_on_two_items_is_a_session_error_and_sends_no_input(qa_game_exe, out_dir, content_file):
    session = start(qa_game_exe, out_dir, content_file)
    calls = session.calls
    with pytest.raises(SessionError, match="no visible ImGui item 'Nope'"):
        session.imgui_click("Nope")
    with pytest.raises(SessionError, match="2 visible ImGui items called 'Close'"):
        session.imgui_click("Close")
    assert session.calls == calls + 2 and len(session.session_errors) == 2
    session.imgui_click("Close", window="Game Panel")
    session.finish()
    methods = [step.method for step in scenario.load(session.scenario_path)[1]]
    assert methods.count("imgui_input") == 1 and methods.count("imgui_find") == 3 and len(methods) == session.calls


def test_the_imgui_calls_are_in_the_recording_and_counted(qa_game_exe, out_dir, content_file):
    session = start(qa_game_exe, out_dir, content_file)
    before = session.calls
    session.imgui_find(kind="window")
    session.imgui_click("Save", window="Scene Editor")
    session.imgui_type("abc")
    session.imgui_key("Escape")
    session.imgui_input([{"type": "move", "x": 1.0, "y": 2.0}, {"type": "down"}])
    assert session.calls == before + 6
    session.finish()
    steps = scenario.load(session.scenario_path)[1]
    assert [step.method for step in steps[-6:]] == ["imgui_find", "imgui_find", "imgui_input", "imgui_input", "imgui_input", "imgui_input"] and len(steps) == session.calls


def test_the_executable_must_belong_to_the_checkout_of_the_content_files(qa_game_exe, out_dir, tmp_path, content_file):
    subprocess.run(["git", "init", "-q", str(content_file.parent.parent)], check=True)
    session = QaSession(out_dir, "a charter")
    with pytest.raises(SessionError, match="not in the checkout"):
        session.launch(exe=str(qa_game_exe), editor=True, content_files=[content_file])
    assert content_file.read_text() == ORIGINAL and not session._game.is_connected


@pytest.fixture
def invariants(tmp_path):
    """A game folder: the rules file marks the root, the module names the content file relative to it."""
    root = tmp_path / "root"
    (root / ".claude" / "rules").mkdir(parents=True)
    (root / ".claude" / "rules" / "qa-project.md").write_text("# QA\n", encoding="utf-8")
    module = root / "qa" / "invariants.py"
    module.parent.mkdir()
    module.write_text('EDITOR_CONTENT_FILES = ["config/SceneConfig.xml"]\n', encoding="utf-8")
    return module


def test_the_invariants_module_names_the_content_files(qa_game_exe, out_dir, content_file, invariants, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    session = QaSession(out_dir, "a charter", invariants=invariants)
    session.launch(exe=str(qa_game_exe), editor=True)
    session.imgui_click("Save", window="Scene Editor")
    finished = session.finish()
    assert content_file.read_text() == ORIGINAL and finished.restored == [content_file.resolve()]
    assert "config/SceneConfig.xml" in finished.report.read_text()


def test_a_malformed_member_of_the_module_is_refused(tmp_path):
    bad = tmp_path / "bad.py"
    bad.write_text('EDITOR_CONTENT_FILES = "config/SceneConfig.xml"\n', encoding="utf-8")
    with pytest.raises(checks.InvariantsError, match="EDITOR_CONTENT_FILES"):
        checks.load_invariants(bad)


def record_a_save(exe, out_dir, content_file, invariants):
    session = QaSession(out_dir, "recorded", invariants=invariants)
    session.launch(exe=str(exe), editor=True)
    session.imgui_click("Save", window="Scene Editor")
    session.act(INC)
    session.act(INC)
    session.finish()
    assert content_file.read_text() == ORIGINAL
    return session.scenario_path


def test_replay_of_a_recorded_editor_session_runs_on_an_overlay_of_its_own(qa_game_exe, out_dir, tmp_path, content_file, invariants, monkeypatch):
    path = record_a_save(qa_game_exe, out_dir, content_file, invariants)
    assert "--editor" in scenario.load(path)[0].flags and not any("overlay" in flag for flag in scenario.load(path)[0].flags)
    args, seen = tmp_path / "args.txt", tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_ARGS_OUT", str(args))
    monkeypatch.setenv("QA_FAKE_OVERLAY_SEEN_OUT", str(seen))
    released = release_calls(monkeypatch)
    header, steps = repro.load_scenario(path)
    run = repro.replay_steps(qa_game_exe, header, steps, checks.load_invariants(invariants))
    assert run.findings == [] and run.played == len(steps)
    assert [restored for _, restored in released] == [[]] and content_file.read_text() == ORIGINAL
    (overlay,) = [arg for arg in args.read_text().split() if arg.startswith("--content-overlay=")]
    assert not overlay.removeprefix("--content-overlay=").startswith(str(out_dir)) and seen.read_text().strip() == "no"


def test_every_probe_of_a_shrink_is_guarded_and_starts_on_a_clean_overlay(qa_game_exe, out_dir, tmp_path, content_file, invariants, monkeypatch):
    monkeypatch.setenv("QA_FAKE_WARN_AT", "2")
    path = record_a_save(qa_game_exe, out_dir, content_file, invariants)
    seen = tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_OVERLAY_SEEN_OUT", str(seen))
    released = release_calls(monkeypatch)
    result = repro.shrink_finding(qa_game_exe, path, "generic.log_warning", invariants)
    assert [call.method for call in result.calls] == ["perform_action"] * 2
    assert len(released) == result.probes + 2  # the whole scenario, each probe, the last play
    assert all(restored == [] for _, restored in released) and seen.read_text().strip() == "no"
    assert content_file.read_text() == ORIGINAL


def test_an_editor_start_with_unknown_content_files_is_refused_before_the_game_starts(qa_game_exe, out_dir, content_file, monkeypatch):
    started = []
    session = QaSession(out_dir, "no content files")
    original = session._game.launch
    monkeypatch.setattr(session._game, "launch", lambda **kwargs: started.append(kwargs) or original(**kwargs))
    with pytest.raises(SessionError, match="content_files"):
        session.launch(exe=str(qa_game_exe), editor=True)
    assert started == []
    session.launch(exe=str(qa_game_exe), editor=True, content_files=[])  # the explicit opt-out
    session.finish()


def test_a_replay_of_an_editor_scenario_with_unknown_content_files_is_a_repro_error_and_exit_2(qa_game_exe, out_dir, content_file, invariants, capsys):
    path = record_a_save(qa_game_exe, out_dir, content_file, invariants)
    header, steps = repro.load_scenario(path)
    with pytest.raises(repro.ReproError, match="content_files"):
        repro.replay_steps(qa_game_exe, header, steps, None)
    assert bagi_qa.main(["replay", "--exe", str(qa_game_exe), str(path)]) == repro.EXIT_USAGE
    assert "content_files" in capsys.readouterr().out
    assert content_file.read_text() == ORIGINAL
