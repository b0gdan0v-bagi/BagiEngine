"""Tests of tools/web/build_and_deploy.py and of the deploy files (Dockerfile, railway.json). Nothing is built or deployed: the
commands are printed (--dry-run) or handed to a fake runner. Run: python -m unittest discover tools/web/tests"""

import contextlib
import io
import json
import re
import tempfile
import unittest
import unittest.mock
from pathlib import Path
from types import SimpleNamespace

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import build_and_deploy

TOOLS_WEB = Path(build_and_deploy.__file__).resolve().parent
ENGINE_DIR = TOOLS_WEB.parents[1]


RAILWAY_PATH = "/usr/bin/railway"
RAILWAY_CMD = r"C:\Users\me\AppData\Roaming\npm\railway.cmd"


def have_railway(name: str) -> str | None:
    return RAILWAY_PATH if name == "railway" else None


def have_railway_cmd(name: str) -> str | None:
    """`which` as it answers on Windows for a Railway CLI installed with npm: the .cmd file, found through PATHEXT."""
    return RAILWAY_CMD if name == "railway" else None


def no_tools(name: str) -> None:
    return None


class FakeRunner:
    """Records the commands instead of running them; `fail_on` is the first word of a command that exits with code 3, `cannot_start`
    the first word of a command that cannot be started (the runner raises FileNotFoundError, as subprocess does)."""

    def __init__(self, fail_on: str | None = None, cannot_start: str | None = None):
        self.calls: list[tuple[list[str], Path]] = []
        self.fail_on = fail_on
        self.cannot_start = cannot_start

    def __call__(self, command, cwd, check):
        self.calls.append((list(command), Path(cwd)))
        if command[0] == self.cannot_start:
            raise FileNotFoundError(2, "The system cannot find the file specified")
        return SimpleNamespace(returncode=3 if command[0] == self.fail_on else 0)


class DeployTestCase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.source = Path(self._temp.name) / "source"
        self.source.mkdir()
        # A fake emsdk folder with the files the SDK check looks for (the real SDK is not needed)
        self.emsdk = Path(self._temp.name) / "emsdk"
        self.install_sdk(self.emsdk)

    @staticmethod
    def install_sdk(emsdk: Path):
        for name in build_and_deploy.SDK_FILES:
            (emsdk / name).parent.mkdir(parents=True, exist_ok=True)
            (emsdk / name).write_text("", encoding="utf-8")

    def run_script(self, *argv, emsdk=None, which=no_tools, runner=None):
        out, err = io.StringIO(), io.StringIO()
        runner = runner or FakeRunner()
        arguments = ["--source-dir", str(self.source), *argv]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = build_and_deploy.run(arguments, emsdk_dir=emsdk or self.emsdk, which=which, runner=runner)
        return code, out.getvalue(), err.getvalue(), runner


class DryRunTest(DeployTestCase):
    def test_a_dry_run_prints_the_three_steps_and_returns_0(self):
        code, out, err, runner = self.run_script("--game", "BagiMatch3", "--dry-run")
        self.assertEqual(code, 0, err)
        lines = [line for line in out.splitlines() if line.startswith("+ ")]
        self.assertEqual(len(lines), 3, out)
        self.assertIn("cmake --preset web-release", lines[0])
        self.assertIn("cmake --build --preset web-release --target BagiMatch3", lines[1])
        self.assertRegex(lines[2].replace('"', "").replace("'", ""), r"package\.py .*web-release[\\/]bin.* .*dist.* --game BagiMatch3")
        self.assertEqual(runner.calls, [])

    def test_a_dry_run_changes_nothing_on_disk(self):
        self.run_script("--dry-run", "--deploy", which=have_railway)
        self.assertEqual(list(self.source.iterdir()), [])

    def test_a_dry_run_with_deploy_adds_the_staging_and_railway_up(self):
        code, out, err, _ = self.run_script("--dry-run", "--deploy", "--service", "my-service", which=have_railway)
        self.assertEqual(code, 0, err)
        lines = out.splitlines()
        up = [line for line in lines if "railway up" in line]
        self.assertEqual(len(up), 1)
        self.assertIn("--path-as-root --service my-service --detach", up[0])
        self.assertIn("web-deploy", up[0])
        self.assertLess(next(i for i, line in enumerate(lines) if line.startswith("stage ")), lines.index(up[0]))

    def test_the_preset_the_game_and_the_folders_can_be_chosen(self):
        build, dist = self.source / "out", self.source / "pkg"
        code, out, _, _ = self.run_script("--dry-run", "--preset", "web-debug", "--game", "BagiMahjong",
                                          "--build-dir", str(build), "--dist", str(dist))
        self.assertEqual(code, 0)
        self.assertIn("--preset web-debug --target BagiMahjong", out)
        self.assertIn(f"package.py {build} {dist} --game BagiMahjong", out.replace("'", ""))

    def test_the_default_game_is_bagimatch3(self):
        _, out, _, _ = self.run_script("--dry-run")
        self.assertIn("--target BagiMatch3", out)

    def test_the_default_is_the_threaded_page_in_build_web_release_bin(self):
        _, out, _, _ = self.run_script("--dry-run")
        self.assertNotIn("single-thread", out)
        self.assertRegex(out.replace('"', "").replace("'", ""), r"package\.py .*[\\/]build[\\/]web-release[\\/]bin ")

    def test_the_package_step_of_a_single_thread_build_says_so_so_the_manifest_has_threads_false(self):
        _, out, _, _ = self.run_script("--dry-run", "--single-thread")
        package_line = next(line for line in out.splitlines() if "package.py" in line)
        self.assertTrue(package_line.rstrip().endswith("--single-thread"), package_line)
        _, out, _, _ = self.run_script("--dry-run", "--preset", "web-debug-single-thread")
        self.assertTrue(next(line for line in out.splitlines() if "package.py" in line).rstrip().endswith("--single-thread"))
        # the default build is threaded: nothing in the package line names the single-thread page
        _, out, _, _ = self.run_script("--dry-run")
        self.assertEqual([line for line in out.splitlines() if "package.py" in line and "--single-thread" in line], [])

    def test_single_thread_picks_the_preset_and_the_folder_of_the_single_thread_page(self):
        for preset, expected in (("web-release", "web-release-single-thread"), ("web-debug", "web-debug-single-thread")):
            with self.subTest(preset=preset):
                code, out, err, runner = self.run_script("--dry-run", "--single-thread", "--preset", preset)
                self.assertEqual(code, 0, err)
                lines = [line for line in out.splitlines() if line.startswith("+ ")]
                self.assertIn(f"cmake --preset {expected}", lines[0])
                self.assertIn(f"cmake --build --preset {expected} --target BagiMatch3", lines[1])
                self.assertRegex(lines[2].replace('"', "").replace("'", ""), rf"package\.py .*[\\/]build[\\/]{expected}[\\/]bin ")
                self.assertEqual(runner.calls, [])

    def test_a_single_thread_preset_named_by_hand_gets_the_single_thread_folder_too(self):
        _, out, _, _ = self.run_script("--dry-run", "--preset", "web-release-single-thread")
        self.assertIn("--preset web-release-single-thread --target", out)
        self.assertNotIn("single-thread-single-thread", out)
        self.assertRegex(out.replace('"', "").replace("'", ""), r"package\.py .*web-release-single-thread[\\/]bin ")

    def test_an_explicit_build_dir_wins_over_the_folder_of_the_configuration(self):
        build = self.source / "out"
        _, out, _, _ = self.run_script("--dry-run", "--single-thread", "--build-dir", str(build))
        self.assertIn(f"package.py {build} ", out.replace("'", ""))
        package_line = next(line for line in out.splitlines() if "package.py" in line)
        self.assertNotIn("web-release-single-thread", package_line)


class PreconditionTest(DeployTestCase):
    def test_without_the_sdk_it_returns_non_zero_with_the_message(self):
        missing = Path(self._temp.name) / "no-sdk"
        incomplete = Path(self._temp.name) / "half-sdk"
        self.install_sdk(incomplete)
        (incomplete / ".emscripten").unlink()  # the install was interrupted before `activate`
        for emsdk in (missing, incomplete):
            with self.subTest(emsdk=emsdk.name):
                code, out, err, runner = self.run_script("--dry-run", emsdk=emsdk)
                self.assertNotEqual(code, 0)
                self.assertIn("run: python CI/scripts/setup_emsdk.py", err)
                self.assertEqual(out, "")
                self.assertEqual(runner.calls, [])

    def test_without_the_sdk_a_real_run_does_not_start_anything_either(self):
        code, _, err, runner = self.run_script(emsdk=Path(self._temp.name) / "no-sdk")
        self.assertNotEqual(code, 0)
        self.assertIn("setup_emsdk.py", err)
        self.assertEqual(runner.calls, [])

    def test_the_environment_variable_emsdk_is_not_read(self):
        # The presets find the SDK in external/emsdk; a variable neither helps nor is needed
        with unittest.mock.patch.dict("os.environ", {"EMSDK": str(self.emsdk)}):
            code, _, err, _ = self.run_script("--dry-run", emsdk=Path(self._temp.name) / "no-sdk")
        self.assertNotEqual(code, 0)
        self.assertIn("setup_emsdk.py", err)
        with unittest.mock.patch.dict("os.environ", {}, clear=True):
            code, _, err, _ = self.run_script("--dry-run")
        self.assertEqual(code, 0, err)

    def test_the_sdk_is_looked_for_in_the_submodule_of_the_engine_by_default(self):
        self.assertEqual(build_and_deploy.EMSDK_DIR, ENGINE_DIR / "external" / "emsdk")
        self.assertTrue((ENGINE_DIR / "CI" / "scripts" / "setup_emsdk.py").is_file())

    def test_deploy_without_the_railway_cli_returns_non_zero_with_the_message(self):
        for extra in ((), ("--dry-run",)):
            with self.subTest(extra=extra):
                code, _, err, runner = self.run_script("--deploy", *extra, which=no_tools)
                self.assertNotEqual(code, 0)
                self.assertIn("Railway CLI", err)
                self.assertEqual(runner.calls, [])

    def test_the_railway_cli_is_not_needed_without_deploy(self):
        code, _, _, _ = self.run_script("--dry-run", which=no_tools)
        self.assertEqual(code, 0)


class RunTest(DeployTestCase):
    def make_dist(self):
        dist = self.source / "dist"
        dist.mkdir()
        (dist / "index.html").write_bytes(b"<html></html>")

    def test_a_run_without_deploy_runs_the_three_commands_in_the_source_folder(self):
        code, _, err, runner = self.run_script("--game", "BagiMatch3")
        self.assertEqual(code, 0, err)
        self.assertEqual([call[0][0] for call in runner.calls[:2]], ["cmake", "cmake"])
        self.assertEqual(len(runner.calls), 3)
        self.assertTrue(all(cwd == self.source.resolve() for _, cwd in runner.calls))
        self.assertFalse((self.source / "build").exists())

    def test_deploy_stages_the_files_and_runs_railway_up_in_the_staged_folder(self):
        dist = self.source / "dist"
        dist.mkdir()
        (dist / "index.html").write_bytes(b"<html></html>")
        (dist / "BagiMatch3.wasm").write_bytes(b"wasm")
        code, _, err, runner = self.run_script("--deploy", "--service", "bagi-web", which=have_railway)
        self.assertEqual(code, 0, err)
        command, cwd = runner.calls[-1]
        self.assertEqual(cwd, self.source.resolve() / "build" / "web-deploy")
        self.assertEqual(command, [RAILWAY_PATH, "up", str(cwd), "--path-as-root", "--service", "bagi-web", "--detach"])
        for name in ("Dockerfile", "railway.json", "serve.py"):
            self.assertEqual((cwd / name).read_bytes(), (TOOLS_WEB / name).read_bytes(), name)
        self.assertEqual((cwd / "dist" / "BagiMatch3.wasm").read_bytes(), b"wasm")

    def test_a_second_stage_replaces_the_old_dist_copy(self):
        dist = self.source / "dist"
        dist.mkdir()
        (dist / "old.js").write_bytes(b"old")
        self.run_script("--deploy", which=have_railway)
        (dist / "old.js").unlink()
        (dist / "new.js").write_bytes(b"new")
        self.run_script("--deploy", which=have_railway)
        staged = self.source / "build" / "web-deploy" / "dist"
        self.assertEqual(sorted(path.name for path in staged.iterdir()), ["new.js"])

    def test_the_program_found_on_the_path_is_the_one_that_runs(self):
        # Windows: CreateProcess does not apply PATHEXT, so a bare `railway` would not find railway.cmd
        self.make_dist()
        code, out, err, runner = self.run_script("--deploy", which=have_railway_cmd)
        self.assertEqual(code, 0, err)
        self.assertEqual(runner.calls[-1][0][0], RAILWAY_CMD)
        self.assertEqual([call[0][0] for call in runner.calls[:2]], ["cmake", "cmake"])
        self.assertIn("railway up", out)
        self.assertNotIn("railway.cmd", out)

    def test_the_deploy_step_is_staged_whatever_the_program_is_called(self):
        self.make_dist()
        self.run_script("--deploy", which=have_railway_cmd)
        self.assertTrue((self.source / "build" / "web-deploy" / "serve.py").is_file())

    def test_a_program_that_cannot_start_ends_the_run_with_a_message_not_a_traceback(self):
        self.make_dist()
        code, _, err, runner = self.run_script("--deploy", which=have_railway_cmd, runner=FakeRunner(cannot_start=RAILWAY_CMD))
        self.assertEqual(code, 2)
        self.assertIn("cannot start", err)
        self.assertIn(RAILWAY_CMD, err)
        self.assertEqual(len(runner.calls), 4)

    def test_a_missing_dist_ends_the_deploy_with_a_message_not_a_traceback(self):
        code, _, err, runner = self.run_script("--deploy", which=have_railway)
        self.assertEqual(code, 2)
        self.assertIn("cannot stage", err)
        self.assertEqual(len(runner.calls), 3)

    def test_a_failing_step_stops_the_run_with_its_exit_code(self):
        code, _, err, runner = self.run_script("--deploy", which=have_railway, runner=FakeRunner(fail_on="cmake"))
        self.assertEqual(code, 3)
        self.assertEqual(len(runner.calls), 1)
        self.assertIn("`cmake` failed", err)


class DeployFilesTest(unittest.TestCase):
    def test_the_dockerfile_copies_only_serve_py_and_dist_and_runs_serve_py(self):
        text = (TOOLS_WEB / "Dockerfile").read_text(encoding="utf-8")
        copies = re.findall(r"^COPY\s+(.+)$", text, flags=re.MULTILINE)
        self.assertEqual(copies, ["serve.py ./", "dist/ dist/"])
        self.assertRegex(text, re.compile(r"^FROM python:[\d.]+-slim", re.MULTILINE))
        self.assertIn('CMD ["python", "serve.py", "--root", "dist"]', text)
        # No switch for the cross-origin isolation: serve.py sends the headers for a threaded page by itself (test_serve.py starts this command)
        self.assertNotIn("cross-origin-isolation", text.split("\nCMD", 1)[1])

    def test_railway_json_builds_the_dockerfile_and_checks_healthz(self):
        config = json.loads((TOOLS_WEB / "railway.json").read_text(encoding="utf-8"))
        self.assertEqual(config["build"]["builder"], "DOCKERFILE")
        self.assertEqual(config["build"]["dockerfilePath"], "Dockerfile")
        self.assertEqual(config["deploy"]["healthcheckPath"], "/healthz")

    def test_the_staged_files_are_the_ones_the_dockerfile_needs(self):
        self.assertEqual(set(build_and_deploy.STAGED_FILES), {"Dockerfile", "railway.json", "serve.py"})
        for name in build_and_deploy.STAGED_FILES:
            self.assertTrue((TOOLS_WEB / name).is_file(), name)

    def test_no_secret_is_written_in_the_deploy_files(self):
        for name in ("build_and_deploy.py", "Dockerfile", "railway.json"):
            text = (TOOLS_WEB / name).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"(?i)(token|password)\s*[=:]\s*[\"'][^\"']+[\"']", name)


if __name__ == "__main__":
    unittest.main()
