import json
import subprocess
import sys

import pytest
from conftest import write_png

import shots

# The game's command for the tests: writes one picture per scene and size into <out>/<label>/ and logs its arguments (one line per run).
FAKE_COMMAND_SCRIPT = '''
import struct, sys, zlib
from pathlib import Path

label, out, scenes, window = sys.argv[1:5]
mode = sys.argv[5] if len(sys.argv) > 5 else ""
if mode == "skip":
    sys.exit(77)
if mode == "fail":
    print("the game crashed")
    sys.exit(3)
folder = Path(out) / label
folder.mkdir(parents=True, exist_ok=True)
with (Path(out) / "runs.log").open("a") as log:
    log.write(" ".join(sys.argv[1:5]) + "\\n")
width, height = (int(part) for part in window.split("x"))
for scene in scenes.split(","):
    if mode == "missing" and scene == "setup":
        continue
    raw = b"".join(b"\\x00" + bytes([len(scene), 0, 0, 255]) * width for _ in range(height))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    (folder / f"{scene}-{window}.png").write_bytes(b"\\x89PNG\\r\\n\\x1a\\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
'''


def make_set(script, mode=""):
    command = ["{python}", str(script), "{label}", "{out}", "{scenes}", "{window}"] + ([mode] if mode else [])
    return {
        "preset": "fake-preset",
        "targets": [],
        "exe": "FakeGame",
        "command": command,
        "scenes": {"fresh-table": "the table after the deal", "setup": "the setup screen"},
        "sizes": ["16x9", "9x16"],
        "defaultSize": "16x9",
    }


def write_rules(root, shots_set):
    rules = root / ".claude" / "rules"
    rules.mkdir(parents=True)
    (rules / "qa-project.md").write_text("# QA\n\n## Other\n\n```json\n{}\n```\n\n## Screenshots\n\n```json\n" + json.dumps(shots_set, indent=2) + "\n```\n", encoding="utf-8")


@pytest.fixture
def game_root(tmp_path):
    script = tmp_path / "fake_command.py"
    script.write_text(FAKE_COMMAND_SCRIPT)
    root = tmp_path / "root"
    write_rules(root, make_set(script))
    return root


@pytest.fixture
def out(tmp_path):
    return tmp_path / "pairs"


def run(capsys, root, *args):
    code = shots.main_with([*args, "--root", str(root)])
    captured = capsys.readouterr()
    return code, captured.out + captured.err


def test_scenes_lists_the_set(game_root, capsys):
    code, text = run(capsys, game_root, "scenes")

    assert code == 0
    assert text.splitlines() == ["sizes: 16x9, 9x16", "default size: 16x9", "fresh-table: the table after the deal", "setup: the setup screen"]


def test_scenes_without_the_file_or_the_section_is_skipped(tmp_path, capsys):
    code, text = run(capsys, tmp_path, "scenes")
    assert code == 77 and text.startswith("no screenshots:")

    (tmp_path / ".claude" / "rules").mkdir(parents=True)
    (tmp_path / ".claude" / "rules" / "qa-project.md").write_text("# QA\n\nnothing here\n")
    code, text = run(capsys, tmp_path, "scenes")
    assert code == 77 and "## Screenshots" in text


def test_a_broken_set_is_an_error(tmp_path, capsys):
    write_rules(tmp_path, {"preset": "p"})

    code, text = run(capsys, tmp_path, "scenes")

    assert code == 1 and "no 'exe'" in text


def test_capture_writes_the_folder_and_groups_the_sizes_into_runs(game_root, out, capsys):
    code, text = run(capsys, game_root, "capture", "--label", "before", "--out", str(out), "--scenes", "fresh-table,setup@9x16,fresh-table@9x16")

    assert code == 0
    assert text.strip() == f"captured 3 pictures into {out / 'before'}"
    assert sorted(path.name for path in (out / "before").glob("*.png")) == ["fresh-table-16x9.png", "fresh-table-9x16.png", "setup-9x16.png"]
    runs = (out / "runs.log").read_text().splitlines()
    assert runs == [f"before {out} fresh-table 16x9", f"before {out} setup,fresh-table 9x16"]


def test_capture_rejects_an_unknown_scene_and_an_unknown_size(game_root, out, capsys):
    code, text = run(capsys, game_root, "capture", "--label", "after", "--out", str(out), "--scenes", "nope")
    assert code == 1 and "unknown scene 'nope'" in text and "fresh-table, setup" in text

    code, text = run(capsys, game_root, "capture", "--label", "after", "--out", str(out), "--scenes", "setup@1x1")
    assert code == 1 and "unknown size '1x1'" in text and "16x9, 9x16" in text
    assert not out.exists()


def test_capture_refuses_an_out_inside_a_git_work_tree(game_root, tmp_path, capsys):
    repo = tmp_path / "repo"
    repo.mkdir()
    if subprocess.run(["git", "init", "-q", str(repo)]).returncode != 0:
        pytest.skip("git is not available")

    code, text = run(capsys, game_root, "capture", "--label", "before", "--out", str(repo / "deeper" / "shots"), "--scenes", "setup")

    assert code == 1 and "inside a git work tree" in text
    assert not (repo / "deeper").exists()


def test_capture_passes_exit_77_through(tmp_path, capsys):
    script = tmp_path / "fake_command.py"
    script.write_text(FAKE_COMMAND_SCRIPT)
    root = tmp_path / "root"
    write_rules(root, make_set(script, "skip"))

    code, text = run(capsys, root, "capture", "--label", "before", "--out", str(tmp_path / "pairs"), "--scenes", "setup")

    assert code == 77 and "skipped" in text


def test_capture_fails_when_the_command_fails_or_a_picture_is_missing(tmp_path, capsys):
    script = tmp_path / "fake_command.py"
    script.write_text(FAKE_COMMAND_SCRIPT)
    out = tmp_path / "pairs"

    root = tmp_path / "root-fail"
    write_rules(root, make_set(script, "fail"))
    code, text = run(capsys, root, "capture", "--label", "before", "--out", str(out), "--scenes", "setup")
    assert code == 1 and "exit code 3" in text and "the game crashed" in text

    root = tmp_path / "root-missing"
    write_rules(root, make_set(script, "missing"))
    code, text = run(capsys, root, "capture", "--label", "before", "--out", str(out), "--scenes", "fresh-table,setup")
    assert code == 1 and "no picture" in text and "setup" in text


def test_a_new_capture_drops_the_pictures_of_the_last_one(game_root, out, capsys):
    run(capsys, game_root, "capture", "--label", "before", "--out", str(out), "--scenes", "fresh-table,setup")
    code, text = run(capsys, game_root, "capture", "--label", "before", "--out", str(out), "--scenes", "setup")

    assert code == 0 and "captured 1 pictures" in text
    assert [path.name for path in (out / "before").glob("*.png")] == ["setup-16x9.png"]


def write_pair(folder, name, width, height, pixel_fn):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / name).write_bytes(write_png(width, height, pixel_fn))


def test_diff_reports_every_kind_of_pair_and_writes_diff_txt(tmp_path, capsys):
    black = lambda x, y: (0, 0, 0, 255)  # noqa: E731
    block = lambda x, y: (255, 255, 255, 255) if 4 <= x <= 11 and 4 <= y <= 11 else (0, 0, 0, 255)  # noqa: E731
    before, after = tmp_path / "before", tmp_path / "after"
    write_pair(before, "a-changed.png", 32, 32, black)
    write_pair(after, "a-changed.png", 32, 32, block)
    write_pair(before, "b-same.png", 16, 16, black)
    write_pair(after, "b-same.png", 16, 16, black)
    write_pair(before, "c-size.png", 16, 8, black)
    write_pair(after, "c-size.png", 8, 16, black)
    write_pair(before, "d-gone.png", 8, 8, black)
    write_pair(after, "e-new.png", 8, 8, black)
    (before / "f-broken.png").write_bytes(b"not a png")
    write_pair(after, "f-broken.png", 8, 8, black)

    code, text = run(capsys, tmp_path, "diff", str(tmp_path))

    assert code == 0
    assert text.splitlines() == [
        "a-changed.png: 6.25% changed (64 of 1024 px), 1 region",
        "  [4,4,11,11] 6.25% of the picture, 100% changed inside",
        "b-same.png: unchanged",
        "c-size.png: size 16x8 -> 8x16, not compared",
        "only in before: d-gone.png",
        "only in after: e-new.png",
        "f-broken.png: cannot be read (not a PNG file: the signature is wrong), not compared",
        "summary: 1 changed, 1 unchanged, 2 not compared, 2 only in one side",
    ]
    assert (tmp_path / "diff.txt").read_text() == text


def test_diff_tolerance_and_missing_folders(tmp_path, capsys):
    write_pair(tmp_path / "before", "a.png", 8, 8, lambda x, y: (10, 10, 10, 255))
    write_pair(tmp_path / "after", "a.png", 8, 8, lambda x, y: (12, 10, 10, 255))

    assert run(capsys, tmp_path, "diff", str(tmp_path))[1].startswith("a.png: 100.00% changed")
    code, text = run(capsys, tmp_path, "diff", str(tmp_path), "--tolerance", "2")
    assert code == 0 and text.startswith("a.png: unchanged")

    code, text = run(capsys, tmp_path, "diff", str(tmp_path / "nothing"))
    assert code == 1 and "no 'before' folder" in text
    (tmp_path / "half" / "before").mkdir(parents=True)
    code, text = run(capsys, tmp_path, "diff", str(tmp_path / "half"))
    assert code == 1 and "no 'after' folder" in text


def test_the_command_line_runs_as_a_script(game_root):
    done = subprocess.run([sys.executable, str(shots.SHOTS_DIR / "shots.py"), "scenes", "--root", str(game_root)], capture_output=True, text=True)

    assert done.returncode == 0 and "fresh-table: the table after the deal" in done.stdout
