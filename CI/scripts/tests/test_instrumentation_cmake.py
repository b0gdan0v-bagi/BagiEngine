"""cmake/BagiFuzz.cmake included alone into a throwaway project (which sanitizer lists and ENGINE_COVERAGE configure, with what flags), and the presets of
CMakePresets.json that switch the instrumentation on. The configure cases are skipped without cmake and a compiler of Clang or GCC,
those that configure a coverage build without clang++."""

import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ENGINE_ROOT = Path(__file__).resolve().parents[3]
SANITIZER_CMAKE = ENGINE_ROOT / "cmake" / "BagiFuzz.cmake"
PRESETS_FILE = ENGINE_ROOT / "CMakePresets.json"

needs_compiler = pytest.mark.skipif(
    shutil.which("cmake") is None or not any(shutil.which(cxx) for cxx in ("c++", "clang++", "g++")),
    reason="needs cmake and a C++ compiler")


def configure(tmp_path: Path, definitions: list[str], before_include: str = "") -> subprocess.CompletedProcess:
    source = tmp_path / "src"
    source.mkdir()
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.31)\nproject(InstrumentationProbe CXX)\n" + before_include +
        f'include("{SANITIZER_CMAKE.as_posix()}")\n'
        "get_directory_property(compile_options COMPILE_OPTIONS)\n"
        "get_directory_property(link_options LINK_OPTIONS)\n"
        'message(STATUS "compile=${compile_options}")\n'
        'message(STATUS "link=${link_options}")\n')
    return subprocess.run(["cmake", "-S", str(source), "-B", str(tmp_path / "build"), *definitions],
                          cwd=tmp_path, capture_output=True, text=True, check=False)


def options(output: str, kind: str) -> list[str]:
    line = next(line for line in output.splitlines() if line.startswith(f"-- {kind}="))
    return line.split("=", 1)[1].split(";")


def refusal(result: subprocess.CompletedProcess) -> str:
    """The error text on one line: cmake wraps the message of a FATAL_ERROR."""
    assert result.returncode != 0, result.stdout
    return " ".join(result.stderr.split())


@needs_compiler
@pytest.mark.skipif(sys.platform == "win32", reason="clang has no ThreadSanitizer runtime on Windows")
@pytest.mark.parametrize("sanitizers", ["thread", "thread;undefined"])
def test_thread_instruments_compile_and_link(tmp_path, sanitizers):
    result = configure(tmp_path, [f"-DENGINE_SANITIZE={sanitizers}"])
    assert result.returncode == 0, result.stdout + result.stderr
    for kind in ("compile", "link"):
        assert "-fsanitize=thread" in options(result.stdout, kind)


@needs_compiler
@pytest.mark.parametrize("definitions", [["-DENGINE_SANITIZE=address;thread"], ["-DENGINE_SANITIZE=thread", "-DENGINE_FUZZ=ON"]],
                         ids=["list", "fuzz"])
def test_thread_with_address_stops_the_configure(tmp_path, definitions):
    assert "thread cannot be combined with address" in refusal(configure(tmp_path, definitions))


@needs_compiler
def test_thread_on_windows_stops_the_configure(tmp_path):
    result = configure(tmp_path, ["-DENGINE_SANITIZE=thread"], before_include="set(WIN32 TRUE)\n")
    assert "thread has no runtime on Windows" in refusal(result)


needs_clang = pytest.mark.skipif(shutil.which("cmake") is None or shutil.which("clang++") is None, reason="needs cmake and clang++")


@needs_clang
@pytest.mark.parametrize("sanitizers", ["", "address;undefined"], ids=["alone", "with-sanitizers"])
def test_coverage_instruments_compile_and_link(tmp_path, sanitizers):
    result = configure(tmp_path, ["-DENGINE_COVERAGE=ON", f"-DENGINE_SANITIZE={sanitizers}", "-DCMAKE_CXX_COMPILER=clang++"])
    assert result.returncode == 0, result.stdout + result.stderr
    compile_options = options(result.stdout, "compile")
    link_options = options(result.stdout, "link")
    profile = next(option for option in compile_options if option.startswith("-fprofile-instr-generate="))
    # The path is baked into the binaries: a run needs no environment variable, and two processes do not write one file
    assert profile.endswith("/build/profraw/%m_%p.profraw"), profile
    assert profile in link_options
    assert "-fcoverage-mapping" in compile_options
    assert "-fcoverage-mapping" not in link_options


@needs_compiler
def test_no_coverage_without_the_option(tmp_path):
    result = configure(tmp_path, [])
    assert result.returncode == 0, result.stdout + result.stderr
    flags = [*options(result.stdout, "compile"), *options(result.stdout, "link")]
    assert not [flag for flag in flags if flag.startswith(("-fprofile", "-fcoverage"))]


@needs_compiler
@pytest.mark.parametrize("before_include, message", [
    ("set(CMAKE_CXX_COMPILER_ID GNU)\n", "ENGINE_COVERAGE needs Clang"),
    ("set(EMSCRIPTEN TRUE)\n", "no coverage on the web"),
], ids=["gcc", "web"])
def test_coverage_without_a_desktop_clang_stops_the_configure(tmp_path, before_include, message):
    assert message in refusal(configure(tmp_path, ["-DENGINE_COVERAGE=ON"], before_include=before_include))


def load_presets() -> dict:
    return json.loads(PRESETS_FILE.read_text(encoding="utf-8"))


def find_preset(presets: dict, kind: str, name: str) -> dict:
    return next(preset for preset in presets[kind] if preset["name"] == name)


def parents_of(preset: dict) -> list[str]:
    parents = preset.get("inherits", [])
    return [parents] if isinstance(parents, str) else parents


def resolve(presets: dict, kind: str, name: str) -> dict:
    """The preset with its parents merged as CMake does it: the preset wins over all, then the first parent that has the field; the
    maps (cache variables, environment) are merged key by key."""
    preset = find_preset(presets, kind, name)
    merged: dict = {}
    for layer in [*(resolve(presets, kind, parent) for parent in reversed(parents_of(preset))), preset]:
        for key, value in layer.items():
            merged[key] = {**merged.get(key, {}), **value} if key in ("cacheVariables", "environment") else value
    return merged


def assert_preset_family(presets: dict, name: str) -> None:
    """A macOS preset has a build preset, a test preset and the four label siblings of the test preset."""
    assert resolve(presets, "buildPresets", name)["configurePreset"] == name
    assert resolve(presets, "testPresets", name)["configurePreset"] == name
    for suffix in ("all", "fast", "smoke", "scenarios"):
        sibling = f"{name}-{suffix}"
        assert parents_of(find_preset(presets, "testPresets", sibling)) == [f"test-{suffix}", name], sibling


def test_macos_tsan_is_an_optimized_build_with_asserts():
    presets = load_presets()
    configure_preset = resolve(presets, "configurePresets", "macos-tsan")
    assert configure_preset["generator"] == "Ninja"
    variables = configure_preset["cacheVariables"]
    assert variables["ENGINE_SANITIZE"] == "thread"
    assert variables["CMAKE_BUILD_TYPE"] == "RelWithDebInfo"
    assert variables["ENGINE_ENABLE_ASSERTS"] == "ON"
    assert variables["ENGINE_ENABLE_EXPECTS"] == "ON"
    assert_preset_family(presets, "macos-tsan")


@pytest.mark.parametrize("name", ["macos-tsan", "macos-tsan-all", "macos-tsan-fast", "macos-tsan-smoke", "macos-tsan-scenarios"])
def test_the_tsan_test_presets_name_a_suppressions_file_that_exists(name):
    # A wrong path stops every instrumented process at its start. The quotes keep a checkout path with a space or a colon whole
    options_text = resolve(load_presets(), "testPresets", name)["environment"]["TSAN_OPTIONS"]
    prefix = "suppressions='${fileDir}/"
    assert options_text.startswith(prefix) and options_text.endswith("'"), options_text
    assert (ENGINE_ROOT / options_text.removeprefix(prefix).removesuffix("'")).is_file()


def test_macos_coverage_is_a_debug_build_that_shares_no_cached_object():
    presets = load_presets()
    configure_preset = resolve(presets, "configurePresets", "macos-coverage")
    assert configure_preset["generator"] == "Ninja"
    variables = configure_preset["cacheVariables"]
    assert variables["ENGINE_COVERAGE"] == "ON"
    assert variables["CMAKE_BUILD_TYPE"] == "Debug"
    # A hit from another checkout would map the coverage to that checkout's sources
    assert variables["ENGINE_CCACHE"] == "OFF"
    assert_preset_family(presets, "macos-coverage")
