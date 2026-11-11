"""The migration tool over toy steps: the registry, the stamp, the order of steps, what happens to a file that fails, the modes of the
command, the game track and the rules of the scan. The real steps have their own tests."""

import subprocess
import sys
from pathlib import Path

import pytest

import migrate

TOOL = Path(migrate.__file__).resolve()

ADD_A = '''
from migrate import step

@step(1, "mark-a")
def mark_a(doc, ctx):
    for e in doc.find_all(lambda e: e.name == "item"):
        e.set("trace", (e.get("trace") or "") + "a")
        ctx.report(e, f"item {e.get('id')} marked")
'''

ADD_B = '''
from migrate import step

@step(2, "mark-b")
def mark_b(doc, ctx):
    for e in doc.find_all(lambda e: e.name == "item"):
        e.set("trace", (e.get("trace") or "") + "b")
        ctx.report(e, f"item {e.get('id')} marked")
'''

FAILS = '''
from migrate import step

@step(1, "refuse")
def refuse(doc, ctx):
    for e in doc.find_all(lambda e: e.name == "item" and e.get("bad")):
        ctx.fail(e, f"item {e.get('id')} cannot be moved")
    for e in doc.find_all(lambda e: e.name == "item"):
        e.set("touched", "1")
'''

GAME_STEP = '''
from migrate import step

@step(1, "game-rename", track="mine")
def rename(doc, ctx):
    for e in doc.find_all(lambda e: e.name == "item"):
        e.set("game", "1")
        ctx.report(e, "game step")
'''


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = text.encode("utf-8")
    path.write_bytes(data)
    return path


def steps_dir(tmp_path: Path, name: str, **modules: str) -> Path:
    folder = tmp_path / name
    folder.mkdir()
    for module, text in modules.items():
        (folder / f"{module}.py").write_text(text)
    return folder


def scene(*items: str, version: str = "") -> str:
    body = "".join(f'    <item id="{i}"/>\n' for i in items)
    return f'<?xml version="1.0"?>\n<SceneManager{version}>\n{body}</SceneManager>\n'


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "config").mkdir(parents=True)
    return root


def run(capsys, root, *args, engine=None):
    code = migrate.main([str(root), *args], engine_steps=engine)
    captured = capsys.readouterr()
    return code, captured.out, captured.err


def test_a_step_runs_stamps_and_reports(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", scene("x", "y"))
    code, out, err = run(capsys, game, engine=engine)
    assert code == 0 and err == ""
    assert (game / "config" / "SceneConfig.xml").read_text() == (
        '<?xml version="1.0"?>\n<SceneManager contentVersion="2">\n    <item id="x" trace="a"/>\n    <item id="y" trace="a"/>\n</SceneManager>\n'
    )
    assert "config/SceneConfig.xml: 1 to 2 (mark-a): 2 elements\n" in out
    assert "  config/SceneConfig.xml:3: item x marked\n" in out
    assert out.strip().endswith("migrate: 1 content file: 1 changed, 0 current, 0 failed")


def test_the_steps_run_in_order_and_the_stamp_is_the_last_version(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0002=ADD_B, s0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", scene("x"))
    code, out, _ = run(capsys, game, engine=engine)
    assert code == 0
    text = (game / "config" / "SceneConfig.xml").read_text()
    assert 'trace="ab"' in text and 'contentVersion="3"' in text
    assert out.index("1 to 2 (mark-a)") < out.index("2 to 3 (mark-b)")


def test_a_file_at_the_middle_version_gets_only_the_later_steps(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A, s0002=ADD_B)
    write(game / "config" / "SceneConfig.xml", scene("x", version=' contentVersion="2"'))
    code, out, _ = run(capsys, game, engine=engine)
    assert code == 0
    text = (game / "config" / "SceneConfig.xml").read_text()
    assert 'trace="b"' in text and 'contentVersion="3"' in text
    assert "1 to 2" not in out


def test_a_file_no_step_changes_still_gets_the_stamp(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", '<SceneManager>\n  <scenes/>\n</SceneManager>\n')
    code, out, _ = run(capsys, game, engine=engine)
    assert code == 0
    assert (game / "config" / "SceneConfig.xml").read_text() == '<SceneManager contentVersion="2">\n  <scenes/>\n</SceneManager>\n'
    assert "1 to 2 (mark-a): no element changed" in out


def test_the_second_run_changes_nothing_and_exits_0(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A, s0002=ADD_B)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    assert run(capsys, game, engine=engine)[0] == 0
    after_first = path.read_bytes()
    mtime = path.stat().st_mtime_ns
    code, out, err = run(capsys, game, engine=engine)
    assert code == 0 and err == ""
    assert path.read_bytes() == after_first and path.stat().st_mtime_ns == mtime
    assert "0 changed, 1 current, 0 failed" in out


def test_a_file_that_fails_stays_untouched_while_another_is_written(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=FAILS)
    bad = write(game / "config" / "A.xml", '<SceneManager>\n  <item id="p"/>\n  <item id="q" bad="1"/>\n</SceneManager>\n')
    good = write(game / "config" / "B.xml", scene("z"))
    bad_bytes = bad.read_bytes()
    code, out, err = run(capsys, game, engine=engine)
    assert code == 2
    assert bad.read_bytes() == bad_bytes
    assert 'contentVersion="2"' in good.read_text()
    assert "config/A.xml:3: refuse: item q cannot be moved" in err
    assert "config/A.xml: not changed" in err
    assert "1 changed, 0 current, 1 failed" in out


def test_a_step_that_raises_leaves_the_file_and_names_the_step(tmp_path, game, capsys):
    boom = 'from migrate import step\n\n@step(1, "boom")\ndef boom(doc, ctx):\n    doc.root.set("x", "1")\n    raise KeyError("nope")\n'
    engine = steps_dir(tmp_path, "engine_steps", s0001=boom)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    before = path.read_bytes()
    code, _, err = run(capsys, game, engine=engine)
    assert code == 2 and path.read_bytes() == before
    assert "the step boom (s0001.py) raised KeyError" in err


def test_check_exits_1_for_a_pending_file_and_writes_nothing(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    before = path.read_bytes()
    code, out, _ = run(capsys, game, "--check", engine=engine)
    assert code == 1 and path.read_bytes() == before
    assert "config/SceneConfig.xml: needs migration (1 to 2)" in out
    assert run(capsys, game, engine=engine)[0] == 0
    assert run(capsys, game, "--check", engine=engine)[0] == 0


def test_check_exits_2_for_an_error_before_1(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    write(game / "config" / "A.xml", scene("x"))
    write(game / "config" / "B.xml", "<SceneManager><unclosed></SceneManager>")
    code, _, err = run(capsys, game, "--check", engine=engine)
    assert code == 2
    assert "config/B.xml:1:" in err and "does not close <unclosed>" in err


def test_check_with_no_steps_and_no_stamp_is_clean(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps")
    write(game / "config" / "SceneConfig.xml", scene("x"))
    code, out, _ = run(capsys, game, "--check", engine=engine)
    assert code == 0 and "1 current" in out


def test_diff_prints_the_difference_and_writes_nothing(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    before = path.read_bytes()
    code, out, _ = run(capsys, game, "--diff", engine=engine)
    assert code == 0 and path.read_bytes() == before
    assert "--- a/config/SceneConfig.xml" in out and "+++ b/config/SceneConfig.xml" in out
    assert '-<SceneManager>\n' in out and '+<SceneManager contentVersion="2">\n' in out
    assert '+    <item id="x" trace="a"/>' in out
    assert "1 would change" in out


def test_crlf_bom_and_a_missing_final_newline_stay(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = game / "config" / "SceneConfig.xml"
    path.write_bytes(b'\xef\xbb\xbf<?xml version="1.0"?>\r\n<SceneManager>\r\n  <item id="x" />\r\n</SceneManager>')
    assert run(capsys, game, engine=engine)[0] == 0
    assert path.read_bytes() == (
        b'\xef\xbb\xbf<?xml version="1.0"?>\r\n<SceneManager contentVersion="2">\r\n  <item id="x" trace="a" />\r\n</SceneManager>'
    )


def test_a_root_with_attributes_on_separate_lines_gets_the_stamp_on_a_line_of_its_own(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "SceneConfig.xml", '<SceneManager\n  a="1"\n  b="2">\n</SceneManager>\n')
    assert run(capsys, game, engine=engine)[0] == 0
    assert path.read_text() == '<SceneManager\n  a="1"\n  b="2"\n  contentVersion="2">\n</SceneManager>\n'


def test_a_node_file_is_a_content_file(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "nodes" / "X.xml", '<node name="X">\n  <item id="1"/>\n</node>\n')
    assert run(capsys, game, engine=engine)[0] == 0
    assert path.read_text() == '<node name="X" contentVersion="2">\n  <item id="1" trace="a"/>\n</node>\n'


@pytest.mark.parametrize(
    "stamp, message",
    [(' contentVersion="two"', "is not a number"), (' contentVersion="-1"', "is not a number"), (' contentVersion=""', "is not a number"),
     (' contentVersion="0"', "older than the first version"), (' contentVersion="9"', "newer than this tool knows (2)")],
)
def test_a_bad_version_is_an_error_of_the_file(tmp_path, game, capsys, stamp, message):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "SceneConfig.xml", scene("x", version=stamp))
    before = path.read_bytes()
    code, _, err = run(capsys, game, engine=engine)
    assert code == 2 and path.read_bytes() == before
    assert message in err and "config/SceneConfig.xml:2:" in err


def test_a_game_track_has_its_own_attribute_and_runs_after_the_engine_track(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    mine = steps_dir(tmp_path, "game_steps", g0001=GAME_STEP)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    code, out, _ = run(capsys, game, "--steps", str(mine), engine=engine)
    assert code == 0
    assert path.read_text() == (
        '<?xml version="1.0"?>\n<SceneManager contentVersion="2" contentVersion.mine="2">\n    <item id="x" trace="a" game="1"/>\n</SceneManager>\n'
    )
    assert out.index("(mark-a)") < out.index("(game-rename)")
    assert run(capsys, game, "--steps", str(mine), "--check", engine=engine)[0] == 0


def test_without_the_game_steps_the_game_attribute_is_not_read(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", scene("x", version=' contentVersion="2" contentVersion.mine="junk"'))
    assert run(capsys, game, "--check", engine=engine)[0] == 0


def test_a_game_track_with_a_bad_version_is_an_error(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps")
    mine = steps_dir(tmp_path, "game_steps", g0001=GAME_STEP)
    write(game / "config" / "SceneConfig.xml", scene("x", version=' contentVersion.mine="5"'))
    code, _, err = run(capsys, game, "--steps", str(mine), engine=engine)
    assert code == 2 and "contentVersion.mine" in err


def test_the_engine_track_belongs_to_the_engine(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps")
    theirs = steps_dir(tmp_path, "game_steps", g0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", scene("x"))
    code, _, err = run(capsys, game, "--steps", str(theirs), engine=engine)
    assert code == 2 and "g0001.py" in err and "engine's" in err


def test_a_gap_a_duplicate_and_a_skipped_version_are_rejected_naming_the_module(tmp_path):
    gap = steps_dir(tmp_path, "gap", s0001=ADD_A, s0003=ADD_B.replace("@step(2,", "@step(3,"))
    with pytest.raises(migrate.RegistryError, match=r"s0003\.py.*starts at version 3, 2 expected"):
        migrate.build_registry(gap)
    start = steps_dir(tmp_path, "start", s0002=ADD_B)
    with pytest.raises(migrate.RegistryError, match=r"s0002\.py.*starts at version 2, 1 expected"):
        migrate.build_registry(start)
    twice = steps_dir(tmp_path, "twice", s0001=ADD_A, s0001b=ADD_A.replace("mark-a", "mark-c"))
    with pytest.raises(migrate.RegistryError, match=r"two steps from version 1: mark-.* and mark-"):
        migrate.build_registry(twice)
    skip = steps_dir(tmp_path, "skip", s0001=ADD_A.replace('"mark-a")', '"mark-a", to_version=3)'))
    with pytest.raises(migrate.RegistryError, match=r"s0001\.py.*goes from 1 to 3"):
        migrate.build_registry(skip)
    same_name = steps_dir(tmp_path, "samename", s0001=ADD_A, s0002=ADD_B.replace("mark-b", "mark-a"))
    with pytest.raises(migrate.RegistryError, match="used twice"):
        migrate.build_registry(same_name)


def test_a_broken_step_module_is_reported_by_name(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001="def (:\n")
    write(game / "config" / "SceneConfig.xml", scene("x"))
    code, _, err = run(capsys, game, engine=engine)
    assert code == 2 and "s0001.py: cannot be loaded: SyntaxError" in err


def test_the_game_may_have_several_tracks_and_they_run_in_name_order(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps")
    zed = GAME_STEP.replace('track="mine"', 'track="zed"').replace('"game-rename"', '"zed-step"').replace('"game", "1"', '"zed", "1"')
    folder = steps_dir(tmp_path, "game_steps", a_mine=GAME_STEP, b_zed=zed)
    write(game / "config" / "SceneConfig.xml", scene("x"))
    code, out, _ = run(capsys, game, "--steps", str(folder), engine=engine)
    assert code == 0 and out.index("(game-rename)") < out.index("(zed-step)")
    assert 'contentVersion.mine="2" contentVersion.zed="2"' in (game / "config" / "SceneConfig.xml").read_text()


# ---- finding the files --------------------------------------------------------------------------------------------------------


def test_the_scan_takes_content_files_of_config_folders_only(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    inside = write(game / "config" / "SceneConfig.xml", scene("x"))
    nested = write(game / "src" / "config" / "nodes" / "N.xml", '<node name="N"/>\n')
    outside = write(game / "data" / "Scene.xml", scene("x"))
    other_root = write(game / "config" / "UIConfig.xml", '<UIConfig><item id="x"/></UIConfig>\n')
    broken_other = write(game / "config" / "Strings.xml", "<Strings><broken></Strings>")
    not_xml = write(game / "config" / "notes.txt", scene("x"))
    others = [outside, other_root, broken_other, not_xml]
    before = {p: p.read_bytes() for p in others}
    code, out, err = run(capsys, game, engine=engine)
    assert code == 0 and err == ""
    assert "contentVersion" in inside.read_text() and "contentVersion" in nested.read_text()
    assert {p: p.read_bytes() for p in others} == before
    assert "2 content files: 2 changed" in out


def test_the_scan_skips_build_git_modules_venv_external_and_a_submodule_engine(tmp_path, game, capsys):
    engine_steps = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    skipped = []
    for folder in ["build", "build-debug", "build/macos/config", ".git", "node_modules", ".venv", "external"]:
        skipped.append(write(game / folder / "config" / "SceneConfig.xml", scene("x")))
    submodule = game / "engine"
    (submodule).mkdir()
    (submodule / ".git").write_text("gitdir: ../.git/modules/engine\n")
    skipped.append(write(submodule / "config" / "SceneConfig.xml", scene("x")))
    write(game / "config" / "SceneConfig.xml", scene("x"))
    before = {p: p.read_bytes() for p in skipped}
    code, out, _ = run(capsys, game, engine=engine_steps)
    assert code == 0 and "1 content file: 1 changed" in out
    assert {p: p.read_bytes() for p in skipped} == before


def test_a_folder_named_engine_that_is_not_the_submodule_is_scanned(tmp_path, game, capsys):
    engine_steps = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "src" / "engine" / "config" / "SceneConfig.xml", scene("x"))
    write(game / "engine" / "config" / "SceneConfig.xml", scene("x"))  # no .git, not the tool's home: an ordinary folder
    code, out, _ = run(capsys, game, engine=engine_steps)
    assert code == 0 and "2 content files: 2 changed" in out and "contentVersion" in path.read_text()


def test_the_engine_checkout_itself_is_scanned_when_it_is_the_root(tmp_path, capsys):
    root = tmp_path / "engine"
    root.mkdir()
    (root / ".git").write_text("gitdir: x\n")
    write(root / "config" / "SceneConfig.xml", scene("x"))
    write(root / "games" / "G" / "config" / "SceneConfig.xml", scene("x"))
    engine_steps = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    code, out, _ = run(capsys, root, engine=engine_steps)
    assert code == 0 and "2 content files: 2 changed" in out


def test_the_root_may_be_the_config_folder_itself(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    assert run(capsys, game / "config", engine=engine)[0] == 0
    assert "contentVersion" in path.read_text()


def test_root_option_and_positional_folder(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    write(game / "config" / "SceneConfig.xml", scene("x"))
    assert migrate.main(["--root", str(game), "--check"], engine_steps=engine) == 1
    capsys.readouterr()
    assert migrate.main(["--check"], engine_steps=engine) in (0, 1, 2)  # the current folder, whatever it holds
    capsys.readouterr()


def test_a_missing_root_is_an_error(tmp_path, capsys):
    assert migrate.main([str(tmp_path / "nope")], engine_steps=steps_dir(tmp_path, "e")) == 2
    assert "is not a folder" in capsys.readouterr().err


def test_the_script_runs_as_a_program_and_steps_import_it_by_name(tmp_path, game):
    engine = steps_dir(tmp_path, "mysteps", g0001=GAME_STEP)
    path = write(game / "config" / "SceneConfig.xml", scene("x"))
    result = subprocess.run(
        [sys.executable, str(TOOL), "--steps", str(engine), str(game)], capture_output=True, text=True, cwd=tmp_path
    )
    assert result.returncode == 0, result.stderr
    assert 'contentVersion.mine="2"' in path.read_text()
    again = subprocess.run([sys.executable, str(TOOL), "--steps", str(engine), "--check", str(game)], capture_output=True, text=True, cwd=tmp_path)
    assert again.returncode == 0


def test_the_engine_steps_folder_loads_with_the_real_registry():
    registry = migrate.build_registry()
    assert registry.latest(migrate.ENGINE_TRACK) >= 1


def test_a_long_comment_before_the_root_does_not_hide_a_content_file(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = write(game / "config" / "nodes" / "Long.xml", '<?xml version="1.0"?>\n<!--\n' + "  a long note\n" * 2000 + '-->\n<node name="L"/>\n')
    assert run(capsys, game, engine=engine)[0] == 0
    assert 'contentVersion="2"' in path.read_text()


def test_a_content_file_that_is_not_utf8_is_an_error_not_a_crash(tmp_path, game, capsys):
    engine = steps_dir(tmp_path, "engine_steps", s0001=ADD_A)
    path = game / "config" / "SceneConfig.xml"
    path.write_bytes(b"<SceneManager>\n\xff</SceneManager>\n")
    code, _, err = run(capsys, game, engine=engine)
    assert code == 2 and "config/SceneConfig.xml:2:1: the file is not UTF-8" in err
