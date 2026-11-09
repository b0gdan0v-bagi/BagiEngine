"""Checks of the Emscripten side that need no Emscripten: the pure functions of cmake/BagiWeb.cmake (run through cmake -P) and the
web presets of CMakePresets.json.

    python -m unittest discover tools/web/tests

The tests that run cmake are skipped when cmake is not on PATH.
"""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[3]
PRESETS_FILE = ENGINE_DIR / "CMakePresets.json"
CHECK_SCRIPT = Path(__file__).resolve().parent / "check_bagi_web.cmake"
WEB_PRESETS = ("web-debug", "web-release")
CMAKE = shutil.which("cmake")


def load_presets() -> dict:
    return json.loads(PRESETS_FILE.read_text(encoding="utf-8"))


def find_preset(presets: dict, kind: str, name: str) -> dict:
    for preset in presets[kind]:
        if preset["name"] == name:
            return preset
    raise KeyError(f"no {kind} preset {name}")


def parents_of(preset: dict) -> list:
    parents = preset.get("inherits", [])
    return [parents] if isinstance(parents, str) else list(parents)


def resolve_configure_preset(presets: dict, name: str) -> dict:
    """The preset with the fields of its parents merged, as CMake does it (later parents win, the child wins over all)."""
    preset = find_preset(presets, "configurePresets", name)
    merged: dict = {"cacheVariables": {}}
    for parent in parents_of(preset):
        resolved = resolve_configure_preset(presets, parent)
        merged.update({key: value for key, value in resolved.items() if key != "cacheVariables"})
        merged["cacheVariables"].update(resolved["cacheVariables"])
    merged.update({key: value for key, value in preset.items() if key != "cacheVariables"})
    merged["cacheVariables"].update(preset.get("cacheVariables", {}))
    return merged


def inherits_from(presets: dict, name: str, ancestor: str) -> bool:
    parents = parents_of(find_preset(presets, "configurePresets", name))
    return ancestor in parents or any(inherits_from(presets, parent, ancestor) for parent in parents)


class WebPresetsTest(unittest.TestCase):
    def setUp(self):
        self.presets = load_presets()

    def build_preset_ancestors(self, name: str) -> list:
        """The build preset and all its ancestors, nearest first."""
        preset = find_preset(self.presets, "buildPresets", name)
        chain = [preset]
        for parent in parents_of(preset):
            chain += self.build_preset_ancestors(parent)
        return chain

    def test_both_configure_presets_are_ninja_builds_of_the_right_type(self):
        debug = resolve_configure_preset(self.presets, "web-debug")
        release = resolve_configure_preset(self.presets, "web-release")
        self.assertEqual(debug["generator"], "Ninja")
        self.assertEqual(release["generator"], "Ninja")
        self.assertEqual(debug["cacheVariables"]["CMAKE_BUILD_TYPE"], "Debug")
        self.assertEqual(release["cacheVariables"]["CMAKE_BUILD_TYPE"], "Release")
        # The release presets of the other platforms switch the asserts off explicitly (cmake-rules.md, "Presets")
        self.assertEqual(release["cacheVariables"]["ENGINE_ENABLE_ASSERTS"], "OFF")
        self.assertEqual(release["cacheVariables"]["ENGINE_ENABLE_EXPECTS"], "OFF")
        self.assertNotIn("ENGINE_ENABLE_ASSERTS", debug["cacheVariables"])

    def test_no_tests_and_no_editor_in_either_preset(self):
        for name in WEB_PRESETS:
            with self.subTest(name):
                variables = resolve_configure_preset(self.presets, name)["cacheVariables"]
                self.assertEqual(variables["ENGINE_BUILD_TESTS"], "OFF")
                self.assertEqual(variables["BE_WITH_EDITOR"], "OFF")

    def test_the_toolchain_file_is_the_emscripten_one_from_the_emsdk_submodule(self):
        # ${fileDir} is the folder of the CMakePresets.json that holds the preset: the engine root, also when a game includes the file
        for name in WEB_PRESETS:
            with self.subTest(name):
                toolchain = resolve_configure_preset(self.presets, name)["toolchainFile"]
                self.assertEqual(toolchain, "${fileDir}/external/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

    def test_the_presets_do_not_depend_on_the_environment_variable_emsdk(self):
        for name in WEB_PRESETS:
            with self.subTest(name):
                self.assertNotIn("condition", resolve_configure_preset(self.presets, name))
                for build_preset in self.build_preset_ancestors(name):
                    self.assertNotIn("condition", build_preset, build_preset["name"])
        self.assertNotIn("EMSDK", PRESETS_FILE.read_text(encoding="utf-8"))

    def test_every_web_configure_preset_has_a_build_preset(self):
        web = [p["name"] for p in self.presets["configurePresets"] if not p.get("hidden") and inherits_from(self.presets, p["name"], "web")]
        self.assertEqual(sorted(web), sorted(WEB_PRESETS))
        build_targets = {p["name"]: p["configurePreset"] for p in self.presets["buildPresets"] if "configurePreset" in p}
        for name in web:
            with self.subTest(name):
                self.assertEqual(build_targets.get(name), name)

    def test_there_is_no_test_preset_for_the_web(self):
        names = {p["name"] for p in self.presets["testPresets"]}
        for name in WEB_PRESETS:
            self.assertNotIn(name, names)

    def test_the_web_presets_do_not_set_per_machine_values(self):
        for name in WEB_PRESETS:
            variables = resolve_configure_preset(self.presets, name)["cacheVariables"]
            for key in ("VULKAN_SDK", "CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER"):
                self.assertNotIn(key, variables)


@unittest.skipUnless(CMAKE, "cmake is not on PATH")
class CmakeListPresetsTest(unittest.TestCase):
    """cmake --list-presets reads and validates the JSON, and shows that the web presets are listed with no EMSDK."""

    def list_presets(self, kind: str, emsdk) -> str:
        env = dict(os.environ)
        env.pop("EMSDK", None)
        if emsdk is not None:
            env["EMSDK"] = emsdk
        result = subprocess.run([CMAKE, f"--list-presets={kind}"], cwd=ENGINE_DIR, env=env, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Error", result.stderr)
        return result.stdout

    def test_the_web_presets_are_listed_with_emsdk_unset_and_set_alike(self):
        for kind in ("configure", "build"):
            for emsdk in (None, "", "C:/nonexistent"):
                with self.subTest(kind=kind, emsdk=emsdk):
                    output = self.list_presets(kind, emsdk)
                    for name in WEB_PRESETS:
                        self.assertIn(f'"{name}"', output)


@unittest.skipUnless(CMAKE, "cmake is not on PATH")
class BagiWebCmakeTest(unittest.TestCase):
    def test_the_pure_functions_of_bagi_web_cmake(self):
        with tempfile.TemporaryDirectory(prefix="bagi_web_cmake_") as scratch:
            result = subprocess.run(
                [CMAKE, f"-DBAGI_ENGINE_DIR={ENGINE_DIR.as_posix()}", f"-DCHECK_DIR={Path(scratch).as_posix()}", "-P", str(CHECK_SCRIPT)],
                capture_output=True,
                text=True,
                check=False,
            )
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("OK", output)


class WebFlagsTextTest(unittest.TestCase):
    """What only a configure with Emscripten shows, read from the text of the files (the first real build confirmed them)."""

    def test_the_release_level_is_o2_by_default_and_is_one_cache_variable(self):
        text = (ENGINE_DIR / "cmake" / "BagiWeb.cmake").read_text(encoding="utf-8")
        self.assertIn('set(BE_WEB_OPTIMIZATION "-O2" CACHE STRING', text)
        self.assertIn('set(CMAKE_CXX_FLAGS_RELEASE "${BE_WEB_OPTIMIZATION} -DNDEBUG")', text)
        self.assertIn('set(CMAKE_C_FLAGS_RELEASE "${BE_WEB_OPTIMIZATION} -DNDEBUG")', text)

    def test_the_web_build_has_no_editor_and_no_warning_about_it_in_debug(self):
        # web-debug sets BE_WITH_EDITOR=OFF; the "ignored in a Debug build" warning must not fire there
        text = (ENGINE_DIR / "cmake" / "BagiEngine.cmake").read_text(encoding="utf-8")
        self.assertRegex(text, r"if\(NOT BE_IS_MULTI_CONFIG AND NOT ENGINE_TARGET_WEB AND CMAKE_BUILD_TYPE STREQUAL \"Debug\"\)\s+message\(WARNING \"BE_WITH_EDITOR=OFF")


if __name__ == "__main__":
    unittest.main()
