"""CI/scripts/coverage_report.py: its exit codes, where it refuses to write, --reset, which sources it leaves out, and a whole run over a
tiny program (skipped without clang and the llvm tools)."""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import coverage_report  # noqa: E402

INSTRUMENTED_IMAGE = b"\x7fELF" + bytes(32) + b"__llvm_covmap" + bytes(32)


def make_build(root: Path, profile: bool = True, image: bytes | None = INSTRUMENTED_IMAGE) -> Path:
    build = root / "build" / "macos-coverage"
    (build / "bin").mkdir(parents=True)
    (build / "profraw").mkdir()
    if image is not None:
        (build / "bin" / "Probe").write_bytes(image)
    if profile:
        (build / "profraw" / "1_2.profraw").write_bytes(b"not read")
    return build


def run(root: Path, *extra: str) -> int:
    return coverage_report.main(["--root", str(root), "--out", str(root.parent / "report"), *extra])


@pytest.mark.parametrize("extra", [
    ["--no-such-option"],
    ["--build", "{root}/missing"],
    ["--profraw", "{root}/missing"],
    ["--object", "{root}/missing"],
    ["--ignore", "("],
    ["--llvm-cov", "{root}/missing"],
], ids=["option", "build", "profraw", "object", "ignore", "tool"])
def test_a_bad_argument_exits_2(tmp_path, extra):
    root = tmp_path / "root"
    make_build(root)
    assert run(root, *[text.format(root=root) for text in extra]) == 2


def test_a_missing_root_exits_2(tmp_path):
    assert coverage_report.main(["--root", str(tmp_path / "missing")]) == 2


@pytest.mark.parametrize("out", ["repo", "repo/build/report"])
def test_an_out_folder_inside_a_git_work_tree_is_refused(tmp_path, capsys, out):
    root = tmp_path / "root"
    make_build(root)
    (tmp_path / "repo" / ".git").mkdir(parents=True)
    assert coverage_report.main(["--root", str(root), "--out", str(tmp_path / out)]) == 2
    assert "inside the git work tree" in capsys.readouterr().err
    assert not (tmp_path / out / "summary.txt").exists()


def test_the_default_out_folder_is_outside_the_checkout_and_names_the_build(tmp_path):
    out = coverage_report.default_out(tmp_path / "Mahjong", tmp_path / "Mahjong" / "build" / "macos-coverage")
    assert out.name == "Mahjong-macos-coverage"
    assert tmp_path / "Mahjong" not in out.parents


def test_a_build_without_a_profile_has_nothing_to_report(tmp_path, capsys):
    root = tmp_path / "root"
    make_build(root, profile=False)
    assert run(root) == 77
    assert "no profile" in capsys.readouterr().err


@pytest.mark.parametrize("image", [None, b"\x7fELF" + bytes(64), b"# a script that names __llvm_covmap\n"],
                         ids=["empty", "plain-executable", "text-file"])
def test_a_build_without_an_instrumented_executable_has_nothing_to_report(tmp_path, capsys, image):
    root = tmp_path / "root"
    make_build(root, image=image)
    assert run(root) == 77
    assert "no executable with a coverage mapping" in capsys.readouterr().err


def test_missing_llvm_tools_have_nothing_to_report(tmp_path, capsys, monkeypatch):
    root = tmp_path / "root"
    make_build(root)
    monkeypatch.setattr(coverage_report, "find_tool", lambda name: None)
    assert run(root) == 77
    assert "llvm-profdata not found" in capsys.readouterr().err


def test_a_mapping_across_two_chunks_is_found(tmp_path, monkeypatch):
    monkeypatch.setattr(coverage_report, "SCAN_CHUNK", 16)
    image = tmp_path / "image"
    image.write_bytes(b"\x7fELF" + bytes(6) + b"__llvm_covmap")
    assert coverage_report.holds_coverage(image)


def test_reset_deletes_the_profiles_and_nothing_else(tmp_path):
    root = tmp_path / "root"
    build = make_build(root)
    (build / "profraw" / "notes.txt").write_text("kept")
    assert coverage_report.main(["--root", str(root), "--reset"]) == 0
    assert sorted(path.name for path in (build / "profraw").iterdir()) == ["notes.txt"]
    assert (build / "bin" / "Probe").is_file()


def test_reset_before_the_first_configure_is_done(tmp_path):
    assert coverage_report.main(["--root", str(tmp_path), "--reset"]) == 0


def ignored(root: Path, path: str, include_tests: bool = False) -> bool:
    patterns = coverage_report.ignore_patterns(root, root / "build" / "macos-coverage", include_tests)
    return any(re.search(pattern, f"{root.as_posix()}/{path}") for pattern in patterns)


@pytest.mark.parametrize("path", [
    "external/SDL/src/SDL.c",
    "engine/external/googletest/src/gtest.cc",
    "src/Modules/fmt/include/fmt/format.h",
    "engine/src/Modules/imgui/imgui.cpp",
    "src/Modules/stb/stb_truetype.h",
    "build/macos-coverage/Generated/Scene.gen.hpp",
    "build/macos-coverage/BagiFuzzIni.cpp",
    "build/other/Generated/Scene.gen.hpp",
    "tests/BECore/SceneTest.cpp",
    "tools/stress/tests/StressTest.cpp",
])
def test_vendored_generated_and_test_sources_are_left_out(tmp_path, path):
    assert ignored(tmp_path, path)


@pytest.mark.parametrize("path", [
    "src/Modules/BECore/Scene/Scene.cpp",
    "engine/src/Modules/Vulkan/src/Vulkan/Device.cpp",
    "src/Modules/Games/src/Games/IGame.h",
    "tools/stress/src/main.cpp",
    "src/Widgets/Viewport/TestsPanel.cpp",
])
def test_engine_and_game_sources_are_reported(tmp_path, path):
    assert not ignored(tmp_path, path)


def test_the_folder_names_above_the_root_do_not_count(tmp_path):
    root = tmp_path / "tests" / "external" / "c++ (copy)"
    assert not ignored(root, "src/Application/Loop.cpp")
    assert ignored(root, "tests/LoopTest.cpp")


def test_include_tests_reports_the_tests(tmp_path):
    assert not ignored(tmp_path, "tests/BECore/SceneTest.cpp", include_tests=True)
    assert ignored(tmp_path, "external/SDL/src/SDL.c", include_tests=True)


def clang() -> str | None:
    if sys.platform == "darwin":
        return coverage_report.find_tool("clang++")
    return shutil.which("clang++")


@pytest.mark.skipif(sys.platform == "win32" or clang() is None or coverage_report.find_tool("llvm-profdata") is None
                    or coverage_report.find_tool("llvm-cov") is None, reason="needs clang and the llvm tools (not on Windows)")
def test_a_whole_run_over_a_tiny_program(tmp_path, capsys):
    root = tmp_path / "root"
    build = make_build(root, profile=False, image=None)
    (root / "src").mkdir()
    (root / "tests").mkdir()
    (root / "src" / "Used.cpp").write_text("int Twice(int value) {\n    return value * 2;\n}\n")
    (root / "tests" / "Main.cpp").write_text("int Twice(int value);\nint main() {\n    return Twice(0);\n}\n")
    program = build / "bin" / "Probe"
    flag = f"-fprofile-instr-generate={build}/profraw/%m_%p.profraw"
    command = [clang(), flag, "-fcoverage-mapping", str(root / "src" / "Used.cpp"), str(root / "tests" / "Main.cpp"), "-o", str(program)]
    if sys.platform == "darwin":
        # The clang that xcrun finds has no default SDK
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True).stdout.strip()
        command += ["-isysroot", sdk]
    subprocess.run(command, check=True)
    environment = {key: value for key, value in os.environ.items() if key != "LLVM_PROFILE_FILE"}
    subprocess.run([str(program)], env=environment, cwd=tmp_path, check=True)

    out = tmp_path / "report"
    assert run(root) == 0
    summary = (out / "summary.txt").read_text()
    assert "Used.cpp" in summary
    assert "Main.cpp" not in summary
    assert (out / "html" / "index.html").is_file()
    printed = capsys.readouterr()
    assert "TOTAL" in printed.out and str(out / "html" / "index.html") in printed.out
    assert "older than an executable" not in printed.err

    # A second report into the same folder, over an executable that is newer than the profile
    newer = program.stat().st_mtime + 60
    os.utime(program, (newer, newer))
    assert run(root, "--include-tests") == 0
    assert "Main.cpp" in (out / "summary.txt").read_text()
    assert "older than an executable" in capsys.readouterr().err

    (build / "profraw" / "broken.profraw").write_bytes(b"cut short")
    assert run(root) == 0, "one unreadable profile must not void the others"
    assert coverage_report.main(["--root", str(root), "--reset"]) == 0
    assert run(root) == 77
