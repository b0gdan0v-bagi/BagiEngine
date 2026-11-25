"""cmake/BagiNotices.cmake included alone into a throwaway project: where the notices file lands for a plain executable, an app bundle
and the web. Skipped without cmake and a C compiler."""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ENGINE_ROOT = Path(__file__).resolve().parents[3]
NOTICES_CMAKE = ENGINE_ROOT / "cmake" / "BagiNotices.cmake"
FILE_NAME = "THIRD_PARTY_NOTICES.txt"
# A component of the engine's table that needs no submodule
COMPONENT = "* EABase (Electronic Arts)"

pytestmark = pytest.mark.skipif(
    shutil.which("cmake") is None or not any(shutil.which(cc) for cc in ("cc", "clang", "gcc")), reason="needs cmake and a C compiler")


def run(command: list[str], cwd: Path) -> None:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr


def make_project(tmp_path: Path, body: str, definitions: list[str]) -> Path:
    source = tmp_path / "src"
    source.mkdir()
    (source / "main.c").write_text("int main(void) { return 0; }\n")
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.31)\nproject(NoticesProbe C)\n"
        "find_package(Python3 COMPONENTS Interpreter REQUIRED)\n"
        f'include("{NOTICES_CMAKE.as_posix()}")\n'
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n" + body)
    build = tmp_path / "build"
    run(["cmake", "-S", str(source), "-B", str(build), *definitions], tmp_path)
    run(["cmake", "--build", str(build)], tmp_path)
    return build / "bin"


def test_a_plain_executable_gets_the_file_next_to_it(tmp_path):
    bin_dir = make_project(tmp_path, "add_executable(Plain main.c)\nbe_add_third_party_notices(Plain)\n", [])
    assert COMPONENT in (bin_dir / FILE_NAME).read_text()


@pytest.mark.skipif(sys.platform != "darwin", reason="an app bundle is a macOS target")
def test_an_app_bundle_gets_the_file_in_its_resources(tmp_path):
    body = ("add_executable(Plain main.c)\nbe_add_third_party_notices(Plain)\n"
            "add_executable(Packed MACOSX_BUNDLE main.c)\nbe_add_third_party_notices(Packed)\n")
    bin_dir = make_project(tmp_path, body, [])
    assert COMPONENT in (bin_dir / "Packed.app" / "Contents" / "Resources" / FILE_NAME).read_text()
    assert COMPONENT in (bin_dir / FILE_NAME).read_text()
    assert not (bin_dir / "Packed.app" / "Contents" / "MacOS" / FILE_NAME).exists()


def test_the_web_gets_the_file_named_after_the_page(tmp_path):
    bin_dir = make_project(tmp_path, "add_executable(Page main.c)\nbe_add_third_party_notices(Page)\n", ["-DENGINE_TARGET_WEB=TRUE"])
    assert COMPONENT in (bin_dir / "Page.notices.txt").read_text()
    assert not (bin_dir / FILE_NAME).exists()


def test_a_game_table_is_added_to_the_file(tmp_path):
    (tmp_path / "game").mkdir()
    (tmp_path / "game" / "THIRD_PARTY_NOTICES.md").write_text(
        "| Path | Component | Version | Licence | Licence file | In a game |\n|---|---|---|---|---|---|\n"
        "| `music.ogg` | Game music | 1 | CC0 | `MUSIC-LICENSE.txt` | ships |\n")
    (tmp_path / "game" / "MUSIC-LICENSE.txt").write_text("CC0 text of the game music\n")
    body = f'add_executable(Plain main.c)\nbe_add_third_party_notices(Plain GAME_NOTICES "{(tmp_path / "game" / "THIRD_PARTY_NOTICES.md").as_posix()}")\n'
    text = (make_project(tmp_path, body, []) / FILE_NAME).read_text()
    assert COMPONENT in text and "* Game music" in text and "CC0 text of the game music" in text
