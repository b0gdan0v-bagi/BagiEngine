"""Tests of tools/web/build_and_deploy.py and of the deploy files (Dockerfile, railway.json). Nothing is built or deployed: the
commands are printed (--dry-run) or handed to a fake runner. Run: python -m unittest discover tools/web/tests"""

import contextlib
import io
import json
import re
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import build_and_deploy

TOOLS_WEB = Path(build_and_deploy.__file__).resolve().parent


def have_railway(name: str) -> str | None:
    return "/usr/bin/railway" if name == "railway" else None


def no_tools(name: str) -> None:
    return None


class FakeRunner:
    """Records the commands instead of running them; `fail_on` is the first word of a command that exits with code 3."""

    def __init__(self, fail_on: str | None = None):
        self.calls: list[tuple[list[str], Path]] = []
        self.fail_on = fail_on

    def __call__(self, command, cwd, check):
        self.calls.append((list(command), Path(cwd)))
        return SimpleNamespace(returncode=3 if command[0] == self.fail_on else 0)


class DeployTestCase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.source = Path(self._temp.name)

    def run_script(self, *argv, environ=None, which=no_tools, runner=None):
        out, err = io.StringIO(), io.StringIO()
        runner = runner or FakeRunner()
        arguments = ["--source-dir", str(self.source), *argv]
        environ = {"EMSDK": "C:/emsdk"} if environ is None else environ
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = build_and_deploy.run(arguments, environ=environ, which=which, runner=runner)
        return code, out.getvalue(), err.getvalue(), runner


class DryRunTest(DeployTestCase):
    def test_a_dry_run_prints_the_three_steps_and_returns_0(self):
        code, out, err, runner = self.run_script("--game", "BagiMatch3", "--dry-run")
        self.assertEqual(code, 0, err)
        lines = [line for line in out.splitlines() if line.startswith("+ ")]
        self.assertEqual(len(lines), 3, out)
        self.assertIn("cmake --preset web-release", lines[0])
        self.assertIn("cmake --build --preset web-release --target BagiMatch3", lines[1])
        self.assertRegex(lines[2].replace('"', "").replace("'", ""), r"package\.py .*ClangWeb.* .*dist.* --game BagiMatch3")
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
        self.assertIn("--service my-service --detach", up[0])
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


class PreconditionTest(DeployTestCase):
    def test_without_emsdk_it_returns_non_zero_with_the_message(self):
        for environ in ({}, {"EMSDK": ""}):
            with self.subTest(environ=environ):
                code, out, err, runner = self.run_script("--dry-run", environ=environ)
                self.assertNotEqual(code, 0)
                self.assertIn("EMSDK is not set", err)
                self.assertEqual(out, "")
                self.assertEqual(runner.calls, [])

    def test_without_emsdk_a_real_run_does_not_start_anything_either(self):
        code, _, err, runner = self.run_script(environ={})
        self.assertNotEqual(code, 0)
        self.assertIn("EMSDK", err)
        self.assertEqual(runner.calls, [])

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
        self.assertEqual(command, ["railway", "up", "--service", "bagi-web", "--detach"])
        self.assertEqual(cwd, self.source.resolve() / "build" / "web-deploy")
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
