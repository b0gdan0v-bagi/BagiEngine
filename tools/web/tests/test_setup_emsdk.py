"""Tests of CI/scripts/setup_emsdk.py: the version/tag check, the "installed" detection on a fake folder, the command lists and the
exit codes of --check. No network and no Emscripten SDK: git and the install are fake runners. Run: python -m unittest discover tools/web/tests"""

import contextlib
import importlib.util
import io
import re
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path
from types import SimpleNamespace

ENGINE_DIR = Path(__file__).resolve().parents[3]
SCRIPT = ENGINE_DIR / "CI" / "scripts" / "setup_emsdk.py"

spec = importlib.util.spec_from_file_location("setup_emsdk", SCRIPT)
setup_emsdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup_emsdk)

VERSION = setup_emsdk.EMSDK_VERSION


def fake_sdk(folder: Path, version: str = VERSION, config: bool = True, driver: bool = True) -> Path:
    """What `emsdk install` and `emsdk activate` leave in the emsdk folder, as far as the script looks."""
    version_file = folder / setup_emsdk.VERSION_FILE
    version_file.parent.mkdir(parents=True, exist_ok=True)
    version_file.write_text(f'"{version}"\n', encoding="utf-8")
    if driver:
        (folder / setup_emsdk.COMPILER_DRIVER).write_text("", encoding="utf-8")
    if config:
        (folder / setup_emsdk.CONFIG_FILE).write_text("", encoding="utf-8")
    return folder


class FakeRunner:
    """Records the commands. `git describe` answers `tag`; the install steps make a fake SDK when `installs`; `fail_on` is a word that,
    found in a command, makes it exit with 1; `cannot_start` is a program that cannot be started."""

    def __init__(self, emsdk: Path, tag: str | None = VERSION, installs: bool = True, fail_on: str | None = None, cannot_start: str | None = None):
        self.emsdk, self.tag, self.installs, self.fail_on, self.cannot_start = emsdk, tag, installs, fail_on, cannot_start
        self.calls: list[list[str]] = []

    def __call__(self, command, cwd=None, check=False, capture_output=False, text=False):
        self.calls.append(list(command))
        if command[0] == self.cannot_start:
            raise FileNotFoundError(2, "The system cannot find the file specified")
        if command[0] == "git" and "describe" in command:
            return SimpleNamespace(returncode=0 if self.tag else 128, stdout=f"{self.tag}\n" if self.tag else "")
        if command[0] == "git" and "submodule" in command:
            (self.emsdk).mkdir(parents=True, exist_ok=True)
            (self.emsdk / "emsdk.py").write_text("", encoding="utf-8")
            return SimpleNamespace(returncode=0, stdout="")
        if self.fail_on and self.fail_on in command:
            return SimpleNamespace(returncode=1, stdout="")
        if self.installs and "activate" in command:
            fake_sdk(self.emsdk)
        return SimpleNamespace(returncode=0, stdout="")


def have_git(name: str) -> str | None:
    return "/usr/bin/git" if name == "git" else None


def no_tools(name: str) -> None:
    return None


class ScriptFileTest(unittest.TestCase):
    def test_the_version_is_one_constant_at_the_top_and_matches_the_gitmodules_comment(self):
        text = SCRIPT.read_text(encoding="utf-8")
        self.assertEqual(re.findall(r'^EMSDK_VERSION = "([^"]+)"$', text, flags=re.MULTILINE), [VERSION])
        gitmodules = (ENGINE_DIR / ".gitmodules").read_text(encoding="utf-8")
        block = gitmodules.split('[submodule "external/emsdk"]')[1].split("[submodule")[0]
        self.assertIn(f"# Fixed at tag: {VERSION}", block)

    def test_the_standard_library_is_all_it_imports(self):
        imports = set(re.findall(r"^(?:import|from) (\w+)", SCRIPT.read_text(encoding="utf-8"), flags=re.MULTILINE))
        self.assertLessEqual(imports, set(sys.stdlib_module_names))

    def test_the_folder_is_external_emsdk_of_the_engine(self):
        self.assertEqual(setup_emsdk.EMSDK_DIR, ENGINE_DIR / "external" / "emsdk")


class InstalledTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.emsdk = Path(self._temp.name) / "emsdk"

    def test_an_empty_or_missing_folder_is_not_installed(self):
        self.assertFalse(setup_emsdk.is_installed(self.emsdk, VERSION))
        self.emsdk.mkdir()
        self.assertFalse(setup_emsdk.is_installed(self.emsdk, VERSION))

    def test_the_pinned_version_with_the_driver_and_the_config_is_installed(self):
        fake_sdk(self.emsdk)
        self.assertTrue(setup_emsdk.is_installed(self.emsdk, VERSION))

    def test_another_version_is_not_installed(self):
        fake_sdk(self.emsdk, version="3.1.0")
        self.assertFalse(setup_emsdk.is_installed(self.emsdk, VERSION))

    def test_an_install_without_activate_is_not_installed(self):
        fake_sdk(self.emsdk, config=False)
        self.assertFalse(setup_emsdk.is_installed(self.emsdk, VERSION))

    def test_a_version_file_without_the_driver_is_not_installed(self):
        fake_sdk(self.emsdk, driver=False)
        self.assertFalse(setup_emsdk.is_installed(self.emsdk, VERSION))

    def test_the_version_file_may_hold_the_version_with_or_without_quotes(self):
        fake_sdk(self.emsdk)
        (self.emsdk / setup_emsdk.VERSION_FILE).write_text(f"{VERSION}\r\n", encoding="utf-8")
        self.assertEqual(setup_emsdk.read_installed_version(self.emsdk), VERSION)


class TagTest(unittest.TestCase):
    def test_the_tag_equal_to_the_constant_passes(self):
        setup_emsdk.check_tag(VERSION, VERSION)

    def test_another_tag_or_none_stops_with_a_message_that_names_both(self):
        for tag in ("4.0.22", "5.0.0", None):
            with self.subTest(tag=tag):
                with self.assertRaises(setup_emsdk.SetupError) as raised:
                    setup_emsdk.check_tag(tag, VERSION)
                self.assertIn(VERSION, str(raised.exception))
                self.assertIn(tag or "not on a tag", str(raised.exception))

    def test_exact_tag_asks_git_in_the_folder_and_reads_the_answer(self):
        emsdk = Path("somewhere") / "emsdk"
        runner = FakeRunner(emsdk, tag="4.0.23")
        self.assertEqual(setup_emsdk.exact_tag(emsdk, runner), "4.0.23")
        self.assertEqual(runner.calls, [["git", "-C", str(emsdk), "describe", "--tags", "--exact-match"]])
        self.assertIsNone(setup_emsdk.exact_tag(emsdk, FakeRunner(emsdk, tag=None)))
        self.assertIsNone(setup_emsdk.exact_tag(emsdk, FakeRunner(emsdk, cannot_start="git")))


class CommandsTest(unittest.TestCase):
    def test_the_commands_are_emsdk_py_install_then_activate_with_the_given_python(self):
        # Windows and macOS (and Linux) alike: the Python that runs the script runs emsdk.py, never emsdk.bat / emsdk.sh
        for python, emsdk in ((r"C:\Python310\python.exe", Path("C:/engine/external/emsdk")), ("/usr/bin/python3", Path("/engine/external/emsdk"))):
            with self.subTest(python=python):
                commands = setup_emsdk.install_commands(emsdk, "4.0.23", python)
                self.assertEqual(commands, [[python, str(emsdk / "emsdk.py"), "install", "4.0.23"],
                                            [python, str(emsdk / "emsdk.py"), "activate", "4.0.23"]])
                joined = " ".join(" ".join(command) for command in commands)
                self.assertNotIn("--permanent", joined)
                self.assertNotIn("emsdk.bat", joined)
                self.assertNotIn("emsdk.sh", joined)

    def test_the_submodule_is_fetched_by_path_and_by_nothing_else(self):
        self.assertEqual(setup_emsdk.submodule_commands(), ["git", "submodule", "update", "--init", "external/emsdk"])


class SetupTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.engine = Path(self._temp.name) / "engine"
        self.emsdk = self.engine / "external" / "emsdk"
        self.emsdk.mkdir(parents=True)
        (self.emsdk / "emsdk.py").write_text("", encoding="utf-8")

    def setup(self, runner, force=False, which=have_git):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = setup_emsdk.setup(self.emsdk, self.engine, VERSION, force, runner=runner, which=which, python="py")
        return code, out.getvalue(), err.getvalue()

    def installs(self, runner) -> list[str]:
        return [command[2] for command in runner.calls if command[0] == "py"]

    def test_an_install_runs_install_then_activate_and_ends_with_the_sdk_installed(self):
        runner = FakeRunner(self.emsdk)
        code, out, err = self.setup(runner)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.installs(runner), ["install", "activate"])
        self.assertTrue(setup_emsdk.is_installed(self.emsdk, VERSION))
        self.assertIn("is installed", out)

    def test_a_second_run_does_nothing_and_asks_nothing_but_the_tag(self):
        fake_sdk(self.emsdk)
        runner = FakeRunner(self.emsdk)
        code, out, _ = self.setup(runner)
        self.assertEqual(code, 0)
        self.assertIn("is installed", out)
        self.assertEqual(self.installs(runner), [])
        self.assertTrue(all(command[0] == "git" and "describe" in command for command in runner.calls), runner.calls)

    def test_force_installs_again(self):
        fake_sdk(self.emsdk)
        runner = FakeRunner(self.emsdk)
        code, _, err = self.setup(runner, force=True)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.installs(runner), ["install", "activate"])

    def test_the_wrong_tag_stops_before_anything_is_installed(self):
        runner = FakeRunner(self.emsdk, tag="4.0.22")
        with self.assertRaises(setup_emsdk.SetupError):
            self.setup(runner)
        self.assertEqual(self.installs(runner), [])

    def test_a_failing_install_returns_1_and_does_not_activate(self):
        runner = FakeRunner(self.emsdk, fail_on="install")
        code, _, err = self.setup(runner)
        self.assertEqual(code, 1)
        self.assertIn("install 4.0.23", err)
        self.assertEqual(self.installs(runner), ["install"])

    def test_an_install_that_leaves_no_sdk_is_a_failure(self):
        code, _, err = self.setup(FakeRunner(self.emsdk, installs=False))
        self.assertEqual(code, 1)
        self.assertIn(VERSION, err)

    def test_the_python_that_cannot_start_is_a_message_not_a_traceback(self):
        code, _, err = self.setup(FakeRunner(self.emsdk, cannot_start="py"))
        self.assertEqual(code, 1)
        self.assertIn("cannot start", err)

    def test_an_empty_submodule_is_initialised_in_the_engine_root_only(self):
        (self.emsdk / "emsdk.py").unlink()
        runner = FakeRunner(self.emsdk)
        calls_cwd = []

        def recording(command, cwd=None, **kwargs):
            calls_cwd.append((list(command), cwd))
            return runner(command, cwd=cwd, **kwargs)

        code, _, err = self.setup(recording)
        self.assertEqual(code, 0, err)
        (update_cwd,) = [cwd for command, cwd in calls_cwd if "submodule" in command]
        self.assertEqual(update_cwd, self.engine)
        update = next(command for command in runner.calls if "submodule" in command)
        self.assertEqual(update, ["git", "submodule", "update", "--init", "external/emsdk"])

    def test_without_git_an_empty_submodule_is_a_clear_error(self):
        (self.emsdk / "emsdk.py").unlink()
        with self.assertRaises(setup_emsdk.SetupError) as raised:
            self.setup(FakeRunner(self.emsdk), which=no_tools)
        self.assertIn("git", str(raised.exception))

    def test_without_git_the_tag_cannot_be_checked(self):
        with self.assertRaises(setup_emsdk.SetupError):
            self.setup(FakeRunner(self.emsdk), which=no_tools)


class CheckTest(unittest.TestCase):
    """--check, as the real program: exit codes, no network, no change."""

    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.emsdk = Path(self._temp.name) / "emsdk"

    def check(self):
        out = io.StringIO()
        with unittest.mock.patch.object(setup_emsdk, "EMSDK_DIR", self.emsdk), contextlib.redirect_stdout(out):
            code = setup_emsdk.main(["--check"])
        return code, out.getvalue()

    def test_installed_exits_0(self):
        fake_sdk(self.emsdk)
        code, out = self.check()
        self.assertEqual(code, 0)
        self.assertIn("is installed", out)

    def test_not_installed_exits_1_with_the_command_to_run_and_changes_nothing(self):
        self.emsdk.mkdir()
        code, out = self.check()
        self.assertEqual(code, 1)
        self.assertIn("setup_emsdk.py", out)
        self.assertEqual(list(self.emsdk.iterdir()), [])

    def test_check_does_not_start_any_program(self):
        fake_sdk(self.emsdk)
        with unittest.mock.patch.object(subprocess, "run") as run:
            self.check()
        run.assert_not_called()

    def test_the_program_itself_exits_with_the_code(self):
        # A real process: --check on this checkout is 0 or 1 (the SDK may or may not be installed here), never a traceback
        result = subprocess.run([sys.executable, str(SCRIPT), "--check"], capture_output=True, text=True, check=False)
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
