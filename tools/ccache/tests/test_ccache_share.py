"""ccache_share.py: the pure parts (ccache text, byte diff, argument list) and the file list on a temporary git repository."""

import subprocess
import sys
from pathlib import Path, PurePath, PureWindowsPath

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import ccache_share as cs  # noqa: E402

STATS = "direct_cache_hit\t30\npreprocessed_cache_hit\t10\ncache_miss\t60\ncache_size_kibibyte\t2048\nbad_input_file\t0\n"

LOG = """\
[2026-11-14T20:52:23.807480 100] Command line: ccache clang++ -c a.cpp -o a.o
[2026-11-14T20:52:23.807757 100] Source file: a.cpp
[2026-11-14T20:52:23.807762 100] Object file: a.o
[2026-11-14T20:52:23.841440 100] Result: cache_miss
[2026-11-14T20:52:23.841444 100] Result: direct_cache_miss
[2026-11-14T20:52:23.841454 100] Result: local_storage_write
[2026-11-14T20:52:23.847790 101] Command line: ccache clang++ -c b.cpp -o b.o
[2026-11-14T20:52:23.848034 101] Source file: b.cpp
[2026-11-14T20:52:23.848038 101] Object file: b.o
[2026-11-14T20:52:23.848382 101] Result: direct_cache_hit
[2026-11-14T20:52:23.848384 101] Result: local_storage_hit
[2026-11-14T20:52:23.854387 102] Command line: ccache clang++ -c c.cpp -o c.o
[2026-11-14T20:52:23.854640 102] Source file: c.cpp
[2026-11-14T20:52:23.854640 103] Command line: ccache clang++ -c d.cpp -o d.o
[2026-11-14T20:52:23.854641 103] Source file: d.cpp
[2026-11-14T20:52:23.854642 103] Object file: d.o
[2026-11-14T20:52:23.885603 103] Result: preprocessed_cache_hit
[2026-11-14T20:52:23.885604 102] Object file: c.o
[2026-11-14T20:52:23.885605 102] Result: could_not_use_precompiled_header
[2026-11-14T20:52:23.885625 102] Result: cache_miss
"""


def test_parse_print_stats_and_summary():
    summary = cs.summarize_stats(cs.parse_print_stats(STATS))
    assert (summary.direct_hits, summary.preprocessed_hits, summary.misses) == (30, 10, 60)
    assert summary.hits == 40
    assert summary.cacheable == 100
    assert summary.hit_rate == pytest.approx(0.4)
    assert summary.cache_size_kib == 2048


def test_empty_stats_have_no_division_by_zero():
    assert cs.summarize_stats(cs.parse_print_stats("")).hit_rate == 0.0


def test_parse_log_outcomes_and_interleaved_processes():
    calls = {call.obj: call for call in cs.parse_log(LOG)}
    assert calls["a.o"].outcome == "miss"
    assert calls["b.o"].outcome == "direct_hit"
    assert calls["d.o"].outcome == "preprocessed_hit"
    # the first non-detail result of c.o was a reason, and the miss that follows it still counts as the miss
    assert calls["c.o"].outcome == "miss"
    assert [call.obj for call in cs.missed(cs.parse_log(LOG))] == ["a.o", "c.o"]


def test_other_outcomes_counts_reasons_without_a_miss():
    text = "[t 7] Command line: ccache clang++ -c x.cpp -o x.o\n[t 7] Result: could_not_use_precompiled_header\n"
    assert cs.other_outcomes(cs.parse_log(text)) == {"could_not_use_precompiled_header": 1}


def test_byte_diff_counts_positions_and_length():
    assert cs.count_differing_bytes(b"abcdef", b"abcdef") == 0
    assert cs.count_differing_bytes(b"abcdef", b"abXdeY") == 2
    assert cs.count_differing_bytes(b"abc", b"abcde") == 2


def test_differing_paths_lists_strings_only_one_side_has():
    a = b"\0\0/work/one/engine/src/a.h\0/usr/include/stdio.h\0/x/y/only_a.h\0\x01/-8E_V\0"
    b = b"\0/work/deeper/two/engine/src/a.h\0\0/usr/include/stdio.h\0\0../../src/only_b.h\0"
    # the same header under another checkout root is no difference; a path with another name is
    assert cs.differing_paths(a, b, "/work/one/engine", "/work/deeper/two/engine") == (["/x/y/only_a.h"], ["../../src/only_b.h"])
    # without the roots the header shows up on both sides
    only_a, only_b = cs.differing_paths(a, b)
    assert "/work/one/engine/src/a.h" in only_a and "/work/deeper/two/engine/src/a.h" in only_b


def test_parse_log_takes_the_padded_pid_of_a_real_log():
    text = "[2026-11-14T20:56:21.902086 9549 ] Command line: ccache clang++ -c a.cpp\n[2026-11-14T20:56:21.9 9549 ] Result: cache_miss\n"
    assert [c.outcome for c in cs.parse_log(text)] == ["miss"]


def test_count_occurrences():
    assert cs.count_occurrences(b"/r/x /r/y /q", "/r") == 2
    assert cs.count_occurrences(b"abc", "") == 0


def test_normalize_argv_joins_a_dash_value():
    argv = ["probe", "--cmake-arg", "-DX=Y", "--targets", "A", "--cmake-arg", "-DENGINE_CCACHE_BASE_DIR="]
    assert cs.normalize_argv(argv) == ["probe", "--cmake-arg=-DX=Y", "--targets", "A", "--cmake-arg=-DENGINE_CCACHE_BASE_DIR="]
    args = cs.build_parser().parse_args(cs.normalize_argv(["probe", "--work", "/w", "--cmake-arg", "-DX=Y", "--cmake-arg", "-DZ=1"]))
    assert args.cmake_arg == ["-DX=Y", "-DZ=1"]


def git(cwd: Path, *args: str) -> None:
    subprocess.run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "-c", "protocol.file.allow=always", *args], cwd=cwd, check=True, capture_output=True)


def test_working_tree_files_and_copy(tmp_path):
    sub = tmp_path / "sub"
    sub.mkdir()
    git(sub, "init", "-q")
    (sub / "lib.c").write_text("int lib;\n")
    git(sub, "add", ".")
    git(sub, "commit", "-qm", "sub")

    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    (repo / "a.txt").write_text("a\n")
    (repo / "gone.txt").write_text("g\n")
    (repo / ".gitignore").write_text("ignored.txt\nbuild/\n")
    git(repo, "add", ".")
    git(repo, "submodule", "add", "-q", str(sub), "external/sub")
    git(repo, "commit", "-qm", "base")
    (repo / "a.txt").write_text("edited\n")  # an uncommitted edit counts
    (repo / "new.txt").write_text("new\n")  # untracked, not ignored
    (repo / "ignored.txt").write_text("x\n")
    (repo / "gone.txt").unlink()  # tracked, deleted on disk

    files = cs.working_tree_files(repo)
    assert "a.txt" in files and "new.txt" in files and "external/sub/lib.c" in files
    assert "ignored.txt" not in files

    dest = tmp_path / "copy"
    copied = cs.copy_working_tree(repo, files, dest)
    assert (dest / "a.txt").read_text() == "edited\n"
    assert (dest / "external" / "sub" / "lib.c").exists()
    assert not (dest / "gone.txt").exists()
    assert copied == len([f for f in files if (repo / f).exists()])


def test_scratch_folder_inside_a_repository_is_refused(tmp_path):
    git(tmp_path, "init", "-q")
    with pytest.raises(cs.ProbeError):
        cs.prepare_scratch(tmp_path / "work", wipe=False)


def test_scratch_folder_must_be_empty_or_ours(tmp_path):
    work = tmp_path / "work"
    cs.prepare_scratch(work, wipe=False)
    (work / "file").write_text("x")
    with pytest.raises(cs.ProbeError):
        cs.prepare_scratch(work, wipe=False)
    cs.prepare_scratch(work, wipe=True)
    assert not (work / "file").exists()
    foreign = tmp_path / "foreign"
    foreign.mkdir()
    (foreign / "keep").write_text("x")
    with pytest.raises(cs.ProbeError):
        cs.prepare_scratch(foreign, wipe=True)
    assert (foreign / "keep").exists()


def test_tool_command_goes_through_vs_env_only_on_windows():
    vs_env = Path("CI/scripts/vs_env.py")
    cmd = ["cmake", "--build", "--preset", "windows-clang-debug"]
    assert cs.tool_command(cmd, windows=False, vs_env=vs_env) == cmd
    assert cs.tool_command(cmd, windows=True, vs_env=vs_env) == [sys.executable, str(vs_env), *cmd]


def test_tool_command_follows_the_platform_by_default(monkeypatch):
    monkeypatch.setattr(cs, "is_windows", lambda: True)
    assert cs.tool_command(["cmake"])[:2] == [sys.executable, str(cs.VS_ENV_SCRIPT)]
    monkeypatch.setattr(cs, "is_windows", lambda: False)
    assert cs.tool_command(["cmake"]) == ["cmake"]


def test_base_dir_argument_uses_forward_slashes():
    assert cs.base_dir_argument(PureWindowsPath(r"D:\cc\p1")) == "-DENGINE_CCACHE_BASE_DIR=D:/cc/p1"
    assert cs.base_dir_argument(PurePath("/tmp/p1")) == "-DENGINE_CCACHE_BASE_DIR=/tmp/p1"


def test_copy_symlink_falls_back_to_a_copy_for_a_file_and_skips_a_folder(tmp_path, monkeypatch, capsys):
    def refused(self, target, target_is_directory=False):
        raise OSError("a required privilege is not held by the client")

    monkeypatch.setattr(Path, "symlink_to", refused)
    file_link = tmp_path / "file_link"
    file_link.write_text("content\n")
    assert cs.copy_symlink(file_link, tmp_path / "copy")
    assert (tmp_path / "copy").read_text() == "content\n"
    folder_link = tmp_path / "folder_link"
    folder_link.mkdir()
    assert not cs.copy_symlink(folder_link, tmp_path / "skipped")
    assert not (tmp_path / "skipped").exists()
    assert "skipped the link" in capsys.readouterr().out


def test_copy_symlink_keeps_a_real_link(tmp_path):
    real = tmp_path / "real.txt"
    real.write_text("x")
    link = tmp_path / "link.txt"
    try:
        link.symlink_to("real.txt")
    except OSError:
        pytest.skip("this account cannot make symlinks")
    assert cs.copy_symlink(link, tmp_path / "copy.txt")
    assert (tmp_path / "copy.txt").is_symlink()


def test_link_directory_uses_a_junction_when_the_symlink_is_refused(tmp_path, monkeypatch):
    def refused(self, target, target_is_directory=False):
        raise OSError("refused")

    calls = []
    monkeypatch.setattr(Path, "symlink_to", refused)
    monkeypatch.setattr(cs, "is_windows", lambda: True)
    monkeypatch.setattr(cs.subprocess, "run", lambda cmd, **kw: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0))
    assert cs.link_directory(tmp_path / "link", tmp_path / "venv") == "junction"
    assert calls == [["cmd", "/c", "mklink", "/J", str(tmp_path / "link"), str(tmp_path / "venv")]]
    monkeypatch.setattr(cs.subprocess, "run", lambda cmd, **kw: subprocess.CompletedProcess(cmd, 1))
    assert cs.link_directory(tmp_path / "link", tmp_path / "venv") == ""
    monkeypatch.setattr(cs, "is_windows", lambda: False)
    assert cs.link_directory(tmp_path / "link", tmp_path / "venv") == ""


def test_long_path_warning_only_past_the_limit():
    assert cs.long_path_warning(Path("D:/cc/p1/deeper/two/engine")) == ""
    assert "shorter --work folder" in cs.long_path_warning(Path("/" + "w" * 120))
    assert cs.long_path_warning(Path("/w"), limit=10) != ""
