"""Tests of tools/web/build_and_deploy.py and of the deploy files (Dockerfile). Nothing is built or deployed: the
commands are printed (--dry-run) or handed to a fake runner. Run: python -m unittest discover tools/web/tests"""

import contextlib
import functools
import io
import json
import re
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path
from types import SimpleNamespace

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import build_and_deploy
import deploy_checkout

TOOLS_WEB = Path(build_and_deploy.__file__).resolve().parent
ENGINE_DIR = TOOLS_WEB.parents[1]
alltests_results = build_and_deploy.alltests_results
GREEN_COMMIT, OTHER_COMMIT = "a" * 40, "b" * 40


def green_gate(source_dir: Path, results_file: Path | None, ignored=()) -> "alltests_results.Verdict":
    """The deploy gate of a commit with a green full test run, so the tests of the other steps need no repository."""
    return alltests_results.Verdict(True, GREEN_COMMIT, "green")


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


class FakeCheckout:
    """Stands in for deploy_checkout.prepare: records what it was asked and hands back a folder that needs no git. A `failure` is
    raised as the CheckoutError a real fetch that went wrong would raise."""

    def __init__(self, checkout: deploy_checkout.Checkout, failure: str | None = None):
        self.checkout = checkout
        self.failure = failure
        self.calls: list[tuple[Path, str, Path, bool]] = []

    def __call__(self, project, base, checkout_dir, *, dry_run):
        self.calls.append((Path(project), base, Path(checkout_dir), dry_run))
        if self.failure:
            raise deploy_checkout.CheckoutError(self.failure)
        return self.checkout


class DeployTestCase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.source = Path(self._temp.name) / "source"
        self.source.mkdir()
        # A fake emsdk folder with the files the SDK check looks for (the real SDK is not needed)
        self.emsdk = Path(self._temp.name) / "emsdk"
        self.install_sdk(self.emsdk)
        # The checkout of --fetch and --target: a folder of its own with an engine that has its own tool files and SDK
        self.checkout_dir = Path(self._temp.name).resolve() / "work" / "checkout"
        self.engine = self.checkout_dir / "engine"
        for name in build_and_deploy.STAGED_FILES:
            (self.engine / "tools" / "web").mkdir(parents=True, exist_ok=True)
            (self.engine / "tools" / "web" / name).write_text(f"checkout {name}", encoding="utf-8")
        self.install_sdk(self.engine / build_and_deploy.EMSDK_RELATIVE)
        self.fake_checkout = FakeCheckout(deploy_checkout.Checkout(self.checkout_dir, GREEN_COMMIT, self.engine, OTHER_COMMIT))

    @staticmethod
    def install_sdk(emsdk: Path):
        for name in build_and_deploy.SDK_FILES:
            (emsdk / name).parent.mkdir(parents=True, exist_ok=True)
            (emsdk / name).write_text("", encoding="utf-8")

    def run_script(self, *argv, emsdk=None, which=no_tools, runner=None, gate=green_gate, checkout=None):
        out, err = io.StringIO(), io.StringIO()
        runner = runner or FakeRunner()
        arguments = ["--source-dir", str(self.source), *argv]
        if ("--target" in argv or "--fetch" in argv) and "--checkout-dir" not in argv:
            arguments += ["--checkout-dir", str(self.checkout_dir)]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = build_and_deploy.run(arguments, emsdk_dir=emsdk or self.emsdk, which=which, runner=runner, gate=gate,
                                     checkout=checkout or self.fake_checkout)
        return code, out.getvalue(), err.getvalue(), runner


class DryRunTest(DeployTestCase):
    def test_a_dry_run_prints_the_three_steps_and_returns_0(self):
        code, out, err, runner = self.run_script("--game", "BagiStress", "--dry-run")
        self.assertEqual(code, 0, err)
        lines = [line for line in out.splitlines() if line.startswith("+ ")]
        self.assertEqual(len(lines), 3, out)
        self.assertIn("cmake --preset web-release", lines[0])
        self.assertIn("cmake --build --preset web-release --target BagiStress", lines[1])
        self.assertRegex(lines[2].replace('"', "").replace("'", ""), r"package\.py .*web-release[\\/]bin.* .*dist.* --game BagiStress")
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

    def test_the_default_game_is_bagistress(self):
        _, out, _, _ = self.run_script("--dry-run")
        self.assertIn("--target BagiStress", out)

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
                self.assertIn(f"cmake --build --preset {expected} --target BagiStress", lines[1])
                self.assertRegex(lines[2].replace('"', "").replace("'", ""), rf"package\.py .*[\\/]build[\\/]{expected}[\\/]bin ")
                self.assertEqual(runner.calls, [])

    def test_a_single_thread_preset_named_by_hand_gets_the_single_thread_folder_too(self):
        _, out, _, _ = self.run_script("--dry-run", "--preset", "web-release-single-thread")
        self.assertIn("--preset web-release-single-thread --target", out)
        self.assertNotIn("single-thread-single-thread", out)
        self.assertRegex(out.replace('"', "").replace("'", ""), r"package\.py .*web-release-single-thread[\\/]bin ")

    def test_the_package_step_gets_the_app_file_of_the_game_when_it_exists(self):
        _, out, _, _ = self.run_script("--dry-run")
        self.assertNotIn("--app", out)
        app = self.source / "web" / "app.json"
        app.parent.mkdir()
        app.write_text("{}", encoding="utf-8")
        _, out, _, _ = self.run_script("--dry-run", "--single-thread")
        package_line = next(line for line in out.splitlines() if "package.py" in line).replace("'", "").replace('"', "")
        self.assertIn(f"--game BagiStress --app {app.resolve()}", package_line)
        self.assertTrue(package_line.rstrip().endswith("--single-thread"), package_line)

    def test_an_app_option_of_the_command_line_wins_over_the_file_of_the_game(self):
        app = self.source / "web" / "app.json"
        app.parent.mkdir()
        app.write_text("{}", encoding="utf-8")
        other = self.source / "other.json"
        _, out, _, _ = self.run_script("--dry-run", "--app", str(other))
        package_line = next(line for line in out.splitlines() if "package.py" in line).replace("'", "").replace('"', "")
        self.assertIn(f"--app {other}", package_line)
        self.assertNotIn(str(app), package_line)

    def test_an_explicit_build_dir_wins_over_the_folder_of_the_configuration(self):
        build = self.source / "out"
        _, out, _, _ = self.run_script("--dry-run", "--single-thread", "--build-dir", str(build))
        self.assertIn(f"package.py {build} ", out.replace("'", ""))
        package_line = next(line for line in out.splitlines() if "package.py" in line)
        self.assertNotIn("web-release-single-thread", package_line)


class TargetTest(DeployTestCase):
    """--target: game, preset and service come from <source>/web/targets.json, and nothing else may be typed."""

    def write_targets(self, **changes):
        entry = {"game": "BagiMahjong", "preset": "web-editor", "service": "studio-mahjong", "address": "studio-mahjong.bagiz.dev", **changes}
        path = self.source / "web" / "targets.json"
        path.parent.mkdir(exist_ok=True)
        path.write_text(json.dumps({"format": 1, "targets": {"mahjong-editor": entry}}), encoding="utf-8")

    def test_a_dry_run_prints_the_steps_of_the_target(self):
        self.write_targets()
        code, out, err, runner = self.run_script("--target", "mahjong-editor", "--dry-run")
        self.assertEqual(code, 0, err)
        self.assertIn("target mahjong-editor: BagiMahjong, web-editor, studio-mahjong, studio-mahjong.bagiz.dev", out)
        lines = [line for line in out.splitlines() if line.startswith("+ ")]
        self.assertEqual(len(lines), 3, out)
        self.assertIn("cmake --preset web-editor", lines[0])
        self.assertIn("cmake --build --preset web-editor --target BagiMahjong", lines[1])
        self.assertIn("--game BagiMahjong", lines[2])
        self.assertEqual(runner.calls, [])

    def test_with_deploy_railway_up_names_the_service_of_the_target(self):
        self.write_targets()
        code, out, err, _ = self.run_script("--target", "mahjong-editor", "--dry-run", "--deploy", which=have_railway)
        self.assertEqual(code, 0, err)
        self.assertIn("--path-as-root --service studio-mahjong --detach", out)

    def test_a_single_thread_preset_of_the_target_packages_a_single_thread_page(self):
        self.write_targets(preset="web-editor-single-thread")
        _, out, _, _ = self.run_script("--target", "mahjong-editor", "--dry-run")
        self.assertIn("--preset web-editor-single-thread", out)
        self.assertIn("--single-thread", out.splitlines()[-1])

    def test_the_free_options_are_refused_and_nothing_starts(self):
        self.write_targets()
        refused = [("--game", "X"), ("--preset", "web-x"), ("--service", "x"), ("--single-thread",), ("--build-dir", "b"), ("--app", "a.json"),
                   ("--dist", "d"), ("--stage-dir", "s"), ("--skip-alltests",), ("--alltests-results", "r.json")]
        for option in refused:
            with self.subTest(option=option[0]):
                code, out, err, runner = self.run_script("--target", "mahjong-editor", *option, which=have_railway)
                self.assertEqual(code, 2)
                self.assertIn(option[0], err)
                self.assertEqual((out, runner.calls), ("", []))

    def test_the_allowed_options_go_with_a_target(self):
        self.write_targets()
        status, checkout = self.source / "status.json", self.source / "checkout"
        code, _, err, _ = self.run_script("--target", "mahjong-editor", "--dry-run", "--deploy", "--status-file", str(status),
                                          "--checkout-dir", str(checkout), which=have_railway)
        self.assertEqual(code, 0, err)

    def test_an_unknown_name_lists_the_known_ones_and_starts_nothing(self):
        self.write_targets()
        code, out, err, runner = self.run_script("--target", "nope", "--dry-run")
        self.assertEqual(code, 2)
        self.assertIn("mahjong-editor", err)
        self.assertEqual((out, runner.calls), ("", []))

    def test_a_missing_or_bad_file_and_a_bad_value_are_exit_2(self):
        code, _, err, runner = self.run_script("--target", "mahjong-editor", "--dry-run")
        self.assertEqual(code, 2)
        self.assertIn("targets.json", err)
        for changes in ({"service": "--x"}, {"address": "https://h/"}, {"game": "a b"}, {"preset": "release"}):
            with self.subTest(changes=changes):
                self.write_targets(**changes)
                code, out, err, runner = self.run_script("--target", "mahjong-editor")
                self.assertEqual(code, 2)
                self.assertEqual((out, runner.calls), ("", []))

    def test_without_target_the_defaults_stay(self):
        self.write_targets()
        _, out, _, _ = self.run_script("--dry-run", "--deploy", which=have_railway)
        self.assertIn("--preset web-release", out)
        self.assertIn("--service bagi-web", out)
        self.assertFalse([line for line in out.splitlines() if line.startswith("target ")])


class StatusFileTest(DeployTestCase):
    """--status-file: the progress of the run, written by deploy_status.py."""

    def status(self) -> dict:
        return json.loads((self.source / "status.json").read_text(encoding="utf-8"))

    def test_a_successful_build_ends_done_in_the_package_stage_and_lists_no_deploy_stage(self):
        code, _, err, _ = self.run_script("--status-file", str(self.source / "status.json"))
        self.assertEqual(code, 0, err)
        data = self.status()
        self.assertEqual((data["state"], data["stage"], data["stages"]), ("done", "package", ["build", "package"]))

    def test_a_failing_cmake_is_failed_at_build_with_the_error(self):
        code, _, err, _ = self.run_script("--status-file", str(self.source / "status.json"), runner=FakeRunner(fail_on="cmake"))
        self.assertEqual(code, 3)
        data = self.status()
        self.assertEqual((data["state"], data["stage"]), ("failed", "build"))
        self.assertIn("`cmake` failed with exit code 3", data["error"])
        self.assertIn("`cmake` failed with exit code 3", err)

    def test_a_failing_package_step_is_failed_at_package(self):
        code, _, _, _ = self.run_script("--status-file", str(self.source / "status.json"), runner=FakeRunner(fail_on=sys.executable))
        self.assertEqual(code, 3)
        self.assertEqual((self.status()["state"], self.status()["stage"]), ("failed", "package"))

    def test_a_deploy_ends_done_in_the_deploy_stage_and_a_missing_dist_is_failed_there(self):
        status_file = str(self.source / "status.json")
        code, _, _, _ = self.run_script("--deploy", "--status-file", status_file, which=have_railway)
        self.assertEqual(code, 2)
        self.assertEqual((self.status()["state"], self.status()["stage"]), ("failed", "deploy"))
        (self.source / "dist").mkdir()
        (self.source / "dist" / "index.html").write_bytes(b"<html></html>")
        code, _, err, _ = self.run_script("--deploy", "--status-file", status_file, which=have_railway)
        self.assertEqual(code, 0, err)
        self.assertEqual((self.status()["state"], self.status()["stage"]), ("done", "deploy"))

    def test_a_refused_gate_a_missing_sdk_and_a_program_that_cannot_start_are_failed(self):
        status_file = str(self.source / "status.json")
        red = lambda *args: alltests_results.Verdict(False, GREEN_COMMIT, "no green run of this commit")
        code, _, _, _ = self.run_script("--deploy", "--status-file", status_file, which=have_railway, gate=red)
        self.assertEqual(code, 2)
        self.assertEqual((self.status()["state"], self.status()["error"]), ("failed", "no green run of this commit"))
        code, _, _, _ = self.run_script("--status-file", status_file, emsdk=self.source / "none")
        self.assertEqual(code, 2)
        self.assertIn("Emscripten SDK", self.status()["error"])
        code, _, _, _ = self.run_script("--status-file", status_file, runner=FakeRunner(cannot_start="cmake"))
        self.assertEqual(code, 2)
        self.assertIn("cannot start `cmake`", self.status()["error"])

    def test_a_dry_run_and_a_run_without_the_option_write_no_status(self):
        self.run_script("--dry-run", "--status-file", str(self.source / "status.json"))
        self.run_script()
        self.assertEqual([entry.name for entry in self.source.iterdir()], [])

    def test_the_target_and_its_service_and_address_are_in_the_file(self):
        (self.source / "web").mkdir()
        (self.source / "web" / "targets.json").write_text(json.dumps({"format": 1, "targets": {"t": {
            "game": "G", "preset": "web-editor", "service": "svc", "address": "svc.example.dev"}}}), encoding="utf-8")
        code, _, err, _ = self.run_script("--target", "t", "--status-file", str(self.source / "status.json"))
        self.assertEqual(code, 0, err)
        data = self.status()
        self.assertEqual((data["target"], data["service"], data["url"]), ("t", "svc", "https://svc.example.dev"))
        self.assertEqual(data["stages"], ["fetch", "build", "package"])


class CheckoutRunTest(DeployTestCase):
    """--fetch: the build runs in the folder `prepare` returned, with the tool files and the SDK of its engine."""

    def status(self) -> dict:
        return json.loads((self.source / "status.json").read_text(encoding="utf-8"))

    def test_the_steps_run_in_the_checkout_and_package_py_is_the_one_of_its_engine(self):
        code, _, err, runner = self.run_script("--fetch")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.fake_checkout.calls, [(self.source.resolve(), "main", self.checkout_dir, False)])
        self.assertEqual({cwd for _, cwd in runner.calls}, {self.checkout_dir})
        self.assertEqual(Path(runner.calls[-1][0][1]), self.engine / "tools" / "web" / "package.py")
        self.assertIn(str(self.checkout_dir / "build" / "web-release" / "bin"), runner.calls[-1][0])
        self.assertEqual(list(self.source.iterdir()), [])

    def test_the_base_of_the_target_is_the_branch_that_is_fetched(self):
        (self.source / "web").mkdir()
        (self.source / "web" / "targets.json").write_text(json.dumps({"format": 1, "targets": {"t": {
            "game": "G", "preset": "web-editor", "service": "svc", "address": "svc.example.dev", "base": "release"}}}), encoding="utf-8")
        self.run_script("--target", "t")
        self.assertEqual(self.fake_checkout.calls[0][1], "release")

    def test_the_staged_files_and_the_gate_are_the_checkouts(self):
        gated = []

        def gate(project, results, ignored):
            gated.append((project, tuple(ignored)))
            return green_gate(project, results)

        (self.checkout_dir / "dist").mkdir()
        (self.checkout_dir / "dist" / "index.html").write_bytes(b"<html></html>")
        code, _, err, runner = self.run_script("--fetch", "--deploy", which=have_railway, gate=gate)
        self.assertEqual(code, 0, err)
        stage_dir = self.checkout_dir / "build" / "web-deploy"
        self.assertEqual(runner.calls[-1], ([RAILWAY_PATH, "up", str(stage_dir), "--path-as-root", "--service", "bagi-web", "--detach"], stage_dir))
        for name in build_and_deploy.STAGED_FILES:
            self.assertEqual((stage_dir / name).read_text(encoding="utf-8"), f"checkout {name}")
        self.assertEqual(gated, [(self.checkout_dir, (self.checkout_dir / "dist", stage_dir))])

    def test_a_failing_checkout_is_exit_2_failed_at_fetch_and_nothing_else_runs(self):
        failing = FakeCheckout(self.fake_checkout.checkout, failure="origin has no branch main")
        code, out, err, runner = self.run_script("--fetch", "--status-file", str(self.source / "status.json"), checkout=failing)
        self.assertEqual(code, 2)
        self.assertIn("origin has no branch main", err)
        self.assertEqual((out, runner.calls), ("", []))
        data = self.status()
        self.assertEqual((data["state"], data["stage"]), ("failed", "fetch"))
        self.assertIn("origin has no branch main", data["error"])

    def test_the_commits_of_the_checkout_are_in_the_status(self):
        code, _, err, _ = self.run_script("--fetch", "--status-file", str(self.source / "status.json"))
        self.assertEqual(code, 0, err)
        data = self.status()
        self.assertEqual((data["commit"], data["engineCommit"], data["stages"]), (GREEN_COMMIT, OTHER_COMMIT, ["fetch", "build", "package"]))

    def test_a_checkout_without_the_sdk_installs_it_first_instead_of_refusing(self):
        (self.engine / build_and_deploy.EMSDK_RELATIVE / ".emscripten").unlink()
        code, _, err, runner = self.run_script("--fetch", "--status-file", str(self.source / "status.json"))
        self.assertEqual(code, 0, err)
        self.assertEqual(runner.calls[0], ([sys.executable, str(self.engine / "CI" / "scripts" / "setup_emsdk.py")], self.checkout_dir))
        self.assertEqual([call[0][0] for call in runner.calls[1:3]], ["cmake", "cmake"])
        code, out, _, runner = self.run_script("--fetch", "--dry-run")
        self.assertEqual(code, 0)
        self.assertRegex(out.splitlines()[0], r"^\+ \(.*checkout\) .*setup_emsdk\.py")

    def test_the_sdk_step_is_not_planned_when_the_checkout_has_the_sdk(self):
        _, _, _, runner = self.run_script("--fetch")
        self.assertNotIn("setup_emsdk.py", " ".join(" ".join(call[0]) for call in runner.calls))

    def test_a_dry_run_asks_for_the_checkout_dry_and_starts_nothing(self):
        def no_gate(*_):
            raise AssertionError("the gate was asked before the fetch")

        code, out, err, runner = self.run_script("--fetch", "--deploy", "--dry-run", which=have_railway, gate=no_gate)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.fake_checkout.calls, [(self.source.resolve(), "main", self.checkout_dir, True)])
        self.assertIn("alltests gate: asked after the fetch, on origin/main", out)
        self.assertEqual(runner.calls, [])
        self.assertFalse(deploy_checkout.lock_path(self.checkout_dir).exists())

    def test_the_lock_is_held_during_the_run_and_a_second_run_is_refused(self):
        lock = deploy_checkout.lock_path(self.checkout_dir)
        seen = []
        original = self.fake_checkout.__call__

        def checking(*args, **kwargs):
            seen.append(lock.exists())
            code, _, err, _ = self.run_script("--fetch")
            seen.append((code, err))
            return original(*args, **kwargs)

        code, _, err, _ = self.run_script("--fetch", checkout=checking)
        self.assertEqual(code, 0, err)
        self.assertTrue(seen[0])
        self.assertEqual(seen[1][0], 2)
        self.assertIn(str(lock), seen[1][1])
        self.assertFalse(lock.exists())


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
        code, _, err, runner = self.run_script("--game", "BagiStress")
        self.assertEqual(code, 0, err)
        self.assertEqual([call[0][0] for call in runner.calls[:2]], ["cmake", "cmake"])
        self.assertEqual(len(runner.calls), 3)
        self.assertTrue(all(cwd == self.source.resolve() for _, cwd in runner.calls))
        self.assertFalse((self.source / "build").exists())

    def test_deploy_stages_the_files_and_runs_railway_up_in_the_staged_folder(self):
        dist = self.source / "dist"
        dist.mkdir()
        (dist / "index.html").write_bytes(b"<html></html>")
        (dist / "BagiStress.wasm").write_bytes(b"wasm")
        code, _, err, runner = self.run_script("--deploy", "--service", "bagi-web", which=have_railway)
        self.assertEqual(code, 0, err)
        command, cwd = runner.calls[-1]
        self.assertEqual(cwd, self.source.resolve() / "build" / "web-deploy")
        self.assertEqual(command, [RAILWAY_PATH, "up", str(cwd), "--path-as-root", "--service", "bagi-web", "--detach"])
        for name in ("Dockerfile", "serve.py"):
            self.assertEqual((cwd / name).read_bytes(), (TOOLS_WEB / name).read_bytes(), name)
        self.assertEqual((cwd / "dist" / "BagiStress.wasm").read_bytes(), b"wasm")

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

    def test_a_file_of_an_earlier_run_is_gone_after_the_next_staging(self):
        self.make_dist()
        staged = self.source / "build" / "web-deploy"
        (staged / "dist" / "sub").mkdir(parents=True)
        (staged / "railway.json").write_text("{}", encoding="utf-8")
        (staged / "dist" / "sub" / "old.js").write_text("old", encoding="utf-8")
        code, _, err, _ = self.run_script("--deploy", which=have_railway)
        self.assertEqual(code, 0, err)
        self.assertEqual(sorted(path.name for path in staged.iterdir()), sorted([*build_and_deploy.STAGED_FILES, "dist"]))
        self.assertEqual([path.name for path in (staged / "dist").iterdir()], ["index.html"])

    def test_a_stage_folder_outside_build_that_holds_files_is_refused_and_kept(self):
        self.make_dist()
        for chosen in (self.source, self.source / "build", self.source / "other"):
            with self.subTest(chosen=chosen.name):
                chosen.mkdir(exist_ok=True)
                keep = chosen / "keep.txt"
                keep.write_text("mine", encoding="utf-8")
                code, _, err, runner = self.run_script("--deploy", "--stage-dir", str(chosen), which=have_railway)
                self.assertEqual(code, 2)
                self.assertIn("will not empty", err)
                self.assertEqual(keep.read_text(encoding="utf-8"), "mine")
                self.assertNotIn(RAILWAY_PATH, [call[0][0] for call in runner.calls])

    def test_a_new_stage_folder_outside_build_is_accepted(self):
        self.make_dist()
        chosen = self.source / "fresh"
        code, _, err, _ = self.run_script("--deploy", "--stage-dir", str(chosen), which=have_railway)
        self.assertEqual(code, 0, err)
        self.assertTrue((chosen / "Dockerfile").is_file())

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


class AlltestsGateTest(DeployTestCase):
    """The gate reads a fake results file of tools/alltests; HEAD of the source is given, so the source needs no repository."""

    def setUp(self):
        super().setUp()
        self.results = Path(self._temp.name) / "results.json"
        data = alltests_results.empty_results("source")
        data["results"][GREEN_COMMIT] = {"status": "green", "preset": "macos-debug-all", "finished": "2026-11-19T10:00:00Z", "failed": []}
        data["results"][OTHER_COMMIT] = {"status": "red", "preset": "macos-debug-all", "finished": "2026-11-19T11:00:00Z", "failed": ["Core.Sorts"]}
        alltests_results.save(self.results, data)
        (self.source / "dist").mkdir()

    def deploy(self, commit: str, *argv: str, changes=None):
        gate = functools.partial(alltests_results.check, head=lambda _: commit, changes=changes or (lambda *_: []))
        return self.run_script("--deploy", "--alltests-results", str(self.results), *argv, which=have_railway, gate=gate)

    def test_a_commit_without_a_result_is_not_deployed_and_the_message_says_how_to_run_the_tests(self):
        code, _, err, runner = self.deploy("c" * 40)
        self.assertEqual(code, 2)
        self.assertEqual(runner.calls, [])
        self.assertIn("alltests.py", err)
        self.assertIn("--commit cccccccccccc", err)
        self.assertIn("--skip-alltests", err)

    def test_a_red_commit_is_not_deployed_and_the_failures_are_named(self):
        code, _, err, runner = self.deploy(OTHER_COMMIT)
        self.assertEqual(code, 2)
        self.assertEqual(runner.calls, [])
        self.assertIn("Core.Sorts", err)

    def test_a_green_commit_is_deployed(self):
        code, out, err, runner = self.deploy(GREEN_COMMIT)
        self.assertEqual(code, 0, err)
        self.assertIn("alltests gate: alltests is green", out)
        self.assertEqual(runner.calls[-1][0][:2], [RAILWAY_PATH, "up"])

    def test_skip_alltests_deploys_anyway_and_says_so(self):
        code, out, err, runner = self.deploy("c" * 40, "--skip-alltests")
        self.assertEqual(code, 0, err)
        self.assertIn("skipped (--skip-alltests)", out)
        self.assertEqual(runner.calls[-1][0][:2], [RAILWAY_PATH, "up"])

    def test_a_dry_run_only_reports_the_gate(self):
        code, out, err, _ = self.deploy("c" * 40, "--dry-run")
        self.assertEqual(code, 0, err)
        self.assertIn("--deploy would stop here", out)
        self.assertIn("railway up", out)

    def test_an_uncommitted_change_is_not_deployed_on_the_green_of_head(self):
        code, _, err, runner = self.deploy(GREEN_COMMIT, changes=lambda *_: [" M src/Game.cpp"])
        self.assertEqual(code, 2)
        self.assertEqual(runner.calls, [])
        self.assertIn("uncommitted changes ( M src/Game.cpp)", err)
        self.assertIn("--skip-alltests", err)

    def test_skip_alltests_deploys_an_uncommitted_tree_anyway(self):
        code, out, err, runner = self.deploy(GREEN_COMMIT, "--skip-alltests", changes=lambda *_: [" M src/Game.cpp"])
        self.assertEqual(code, 0, err)
        self.assertIn("skipped (--skip-alltests)", out)
        self.assertEqual(runner.calls[-1][0][:2], [RAILWAY_PATH, "up"])

    def test_the_gate_leaves_out_what_the_script_writes_itself_dist_and_the_stage(self):
        asked = []
        self.deploy(GREEN_COMMIT, changes=lambda _, ignored: asked.extend(ignored) or [])
        self.assertEqual([path.resolve() for path in asked], [(self.source / folder).resolve() for folder in ("dist", build_and_deploy.STAGE_FOLDER)])

    def test_without_deploy_the_gate_is_not_asked(self):
        def no_gate(*_):
            raise AssertionError("the gate was asked")

        code, _, err, _ = self.run_script("--dry-run", gate=no_gate)
        self.assertEqual(code, 0, err)


class DeployFilesTest(unittest.TestCase):
    def test_the_dockerfile_copies_only_serve_py_and_dist_and_runs_serve_py(self):
        text = (TOOLS_WEB / "Dockerfile").read_text(encoding="utf-8")
        copies = re.findall(r"^COPY\s+(.+)$", text, flags=re.MULTILINE)
        self.assertEqual(copies, ["serve.py ./", "dist/ dist/"])
        self.assertRegex(text, re.compile(r"^FROM python:[\d.]+-slim", re.MULTILINE))
        self.assertIn('CMD ["python", "serve.py", "--root", "dist"]', text)
        # No switch for the cross-origin isolation: serve.py sends the headers for a threaded page by itself (test_serve.py starts this command)
        self.assertNotIn("cross-origin-isolation", text.split("\nCMD", 1)[1])

    def test_no_config_as_code_file_is_staged(self):
        # Railway stops reading railway.json / railway.toml on 2026-12-01: the service settings live in the game's IaC file
        for name in ("railway.json", "railway.toml"):
            self.assertNotIn(name, build_and_deploy.STAGED_FILES)
            self.assertFalse((TOOLS_WEB / name).exists(), name)

    def test_the_staged_files_are_the_ones_the_dockerfile_needs(self):
        self.assertEqual(set(build_and_deploy.STAGED_FILES), {"Dockerfile", "serve.py"})
        for name in build_and_deploy.STAGED_FILES:
            self.assertTrue((TOOLS_WEB / name).is_file(), name)

    def test_no_secret_is_written_in_the_deploy_files(self):
        for name in ("build_and_deploy.py", "Dockerfile"):
            text = (TOOLS_WEB / name).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"(?i)(token|password)\s*[=:]\s*[\"'][^\"']+[\"']", name)


if __name__ == "__main__":
    unittest.main()
