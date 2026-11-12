"""Tests of tools/rooms/build_and_deploy.py and of the runtime Dockerfile it stages. Nothing is built or deployed: the commands are printed
(--dry-run) or handed to a fake runner. Run: python3 -m unittest discover tools/rooms/tests"""

import contextlib
import io
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

# tools/rooms, where build_and_deploy.py is (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import build_and_deploy

TOOLS_ROOMS = Path(build_and_deploy.__file__).resolve().parent
DOCKER_PATH = "/usr/bin/docker"
RAILWAY_PATH = "/usr/bin/railway"
RAILWAY_CMD = r"C:\Users\me\AppData\Roaming\npm\railway.cmd"
BINARY_BYTES = b"\x7fELF fake server"


def have_tools(name: str) -> str | None:
    return {"docker": DOCKER_PATH, "railway": RAILWAY_PATH}.get(name)


def have_railway_cmd(name: str) -> str | None:
    """`which` as it answers on Windows for a Railway CLI installed with npm: the .cmd file, found through PATHEXT."""
    return {"docker": DOCKER_PATH, "railway": RAILWAY_CMD}.get(name)


def only_docker(name: str) -> str | None:
    return DOCKER_PATH if name == "docker" else None


def no_tools(name: str) -> None:
    return None


class FakeRunner:
    """Records the commands instead of running them. `docker cp` writes the files a build would leave in /out into its destination
    (`out_files`: name -> bytes, none for a build that made nothing). `fail_on` is the second word of a command that exits with code 3
    (build, create, cp, rm), `cannot_start` the first word of a command that cannot be started (the runner raises FileNotFoundError, as subprocess does)."""

    def __init__(self, fail_on: str | None = None, cannot_start: str | None = None, out_files: dict[str, bytes] | None = None):
        self.calls: list[tuple[list[str], Path]] = []
        self.fail_on = fail_on
        self.cannot_start = cannot_start
        self.out_files = {build_and_deploy.BINARY_NAME: BINARY_BYTES} if out_files is None else out_files

    def __call__(self, command, cwd, check):
        self.calls.append((list(command), Path(cwd)))
        if command[0] == self.cannot_start:
            raise FileNotFoundError(2, "The system cannot find the file specified")
        if command[1] == self.fail_on:
            return SimpleNamespace(returncode=3)
        if command[1] == "cp":
            destination = Path(command[3])
            for name, content in self.out_files.items():
                (destination / name).write_bytes(content)
        return SimpleNamespace(returncode=0)

    def words(self) -> list[str]:
        return [call[0][1] for call in self.calls]


class DeployTestCase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.source = Path(self._temp.name) / "source"
        self.source.mkdir()
        self.stage = self.source / "build" / "rooms-deploy"

    def run_script(self, *argv, which=have_tools, runner=None):
        out, err = io.StringIO(), io.StringIO()
        runner = runner or FakeRunner()
        arguments = ["--source-dir", str(self.source), *argv]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = build_and_deploy.run(arguments, which=which, runner=runner)
        return code, out.getvalue(), err.getvalue(), runner


class DryRunTest(DeployTestCase):
    def commands(self, out: str) -> list[str]:
        return [line for line in out.splitlines() if line.startswith("+ ")]

    def test_a_dry_run_prints_build_create_cp_rm_and_runs_nothing(self):
        code, out, err, runner = self.run_script("--dry-run")
        self.assertEqual(code, 0, err)
        lines = self.commands(out)
        self.assertEqual(len(lines), 4, out)
        self.assertIn("docker build --platform linux/amd64 --target build -f", lines[0])
        self.assertIn("--build-arg SERVER_TARGET=BagiRoomServer --build-arg PRESET=linux-server-release", lines[0])
        self.assertIn("docker create --platform linux/amd64 --name bagi-room-extract-", lines[1])
        self.assertRegex(lines[2], r"docker cp bagi-room-extract-\d+:/out/\. .*rooms-deploy.app")
        self.assertRegex(lines[3], r"docker rm -f bagi-room-extract-\d+")
        self.assertEqual(runner.calls, [])

    def test_a_dry_run_changes_nothing_on_disk(self):
        self.run_script("--dry-run", "--deploy")
        self.assertEqual(list(self.source.iterdir()), [])

    def test_the_build_context_is_the_source_and_the_dockerfile_is_the_one_of_the_engine(self):
        _, out, _, _ = self.run_script("--dry-run")
        build = self.commands(out)[0]
        self.assertIn(f"-f {build_and_deploy.BUILD_DOCKERFILE}", build)
        self.assertTrue(build.rstrip().endswith(str(self.source.resolve())), build)
        self.assertTrue(build_and_deploy.BUILD_DOCKERFILE.is_file())

    def test_the_target_and_the_preset_of_a_game_can_be_chosen(self):
        _, out, _, _ = self.run_script("--dry-run", "--server-target", "BagiMahjongServer", "--preset", "game-linux-server")
        build = self.commands(out)[0]
        self.assertIn("--build-arg SERVER_TARGET=BagiMahjongServer --build-arg PRESET=game-linux-server", build)
        self.assertIn("-t bagi-room-build-bagimahjongserver", build)
        self.assertIn("bagi-room-build-bagimahjongserver", self.commands(out)[1])

    def test_a_dry_run_with_deploy_adds_railway_up_on_the_stage_folder(self):
        code, out, err, _ = self.run_script("--dry-run", "--deploy", "--service", "my-service")
        self.assertEqual(code, 0, err)
        lines = self.commands(out)
        self.assertEqual(len(lines), 5)
        self.assertIn(f"railway up {self.stage.resolve()} --path-as-root --service my-service --detach", lines[4])

    def test_a_dry_run_without_deploy_prints_no_railway_command(self):
        _, out, _, _ = self.run_script("--dry-run")
        self.assertNotIn("railway", out)

    def test_the_stage_folder_can_be_chosen(self):
        other = self.source / "elsewhere"
        _, out, _, _ = self.run_script("--dry-run", "--deploy", "--stage-dir", str(other))
        self.assertIn(f"{other.resolve() / 'app'}", out)
        self.assertIn(f"railway up {other.resolve()} ", out)


class PreconditionTest(DeployTestCase):
    def test_without_docker_it_returns_non_zero_with_the_message(self):
        for extra in ((), ("--dry-run",)):
            with self.subTest(extra=extra):
                code, out, err, runner = self.run_script(*extra, which=no_tools)
                self.assertNotEqual(code, 0)
                self.assertIn("Docker", err)
                self.assertEqual(out, "")
                self.assertEqual(runner.calls, [])

    def test_deploy_without_the_railway_cli_returns_non_zero_with_the_message(self):
        for extra in ((), ("--dry-run",)):
            with self.subTest(extra=extra):
                code, _, err, runner = self.run_script("--deploy", *extra, which=only_docker)
                self.assertNotEqual(code, 0)
                self.assertIn("Railway CLI", err)
                self.assertEqual(runner.calls, [])

    def test_the_railway_cli_is_not_needed_without_deploy(self):
        code, _, _, _ = self.run_script("--dry-run", which=only_docker)
        self.assertEqual(code, 0)


class RunTest(DeployTestCase):
    def test_a_run_without_deploy_builds_extracts_removes_and_stages(self):
        code, out, err, runner = self.run_script()
        self.assertEqual(code, 0, err)
        self.assertEqual(runner.words(), ["build", "create", "cp", "rm"])
        self.assertTrue(all(call[0][0] == DOCKER_PATH for call in runner.calls))
        self.assertEqual((self.stage / "app" / "room-server").read_bytes(), BINARY_BYTES)
        self.assertEqual((self.stage / "Dockerfile").read_bytes(), (TOOLS_ROOMS / "Dockerfile.prebuilt").read_bytes())
        self.assertEqual(sorted(path.name for path in self.stage.iterdir()), ["Dockerfile", "app"])
        self.assertIn("staged", out)

    def test_the_container_is_created_copied_from_and_removed_by_the_same_name(self):
        _, _, _, runner = self.run_script()
        name = runner.calls[1][0][runner.calls[1][0].index("--name") + 1]
        self.assertTrue(runner.calls[2][0][2].startswith(f"{name}:/out/"))
        self.assertEqual(runner.calls[3][0][-1], name)

    def test_deploy_runs_railway_up_in_the_stage_after_the_stage_is_complete(self):
        seen = {}

        class Checking(FakeRunner):
            def __call__(self, command, cwd, check):
                if command[0] == RAILWAY_PATH:
                    seen["files"] = sorted(path.name for path in Path(command[2]).rglob("*") if path.is_file())
                return super().__call__(command, cwd, check)

        code, _, err, runner = self.run_script("--deploy", "--service", "bagi-rooms", runner=Checking())
        self.assertEqual(code, 0, err)
        command, cwd = runner.calls[-1]
        self.assertEqual(cwd, self.stage.resolve())
        self.assertEqual(command, [RAILWAY_PATH, "up", str(cwd), "--path-as-root", "--service", "bagi-rooms", "--detach"])
        self.assertEqual(seen["files"], ["Dockerfile", "room-server"])

    def test_shared_libraries_taken_out_of_the_build_travel_with_the_binary(self):
        runner = FakeRunner(out_files={"room-server": BINARY_BYTES, "libextra.so.1": b"lib"})
        code, _, err, _ = self.run_script("--deploy", runner=runner)
        self.assertEqual(code, 0, err)
        self.assertEqual(sorted(path.name for path in (self.stage / "app").iterdir()), ["libextra.so.1", "room-server"])

    def test_a_second_run_leaves_no_file_of_the_first_one(self):
        self.run_script(runner=FakeRunner(out_files={"room-server": b"1", "old.so": b"old"}))
        self.run_script()
        self.assertEqual(sorted(path.name for path in (self.stage / "app").iterdir()), ["room-server"])

    def test_the_program_found_on_the_path_is_the_one_that_runs(self):
        # Windows: CreateProcess does not apply PATHEXT, so a bare `railway` would not find railway.cmd
        code, out, err, runner = self.run_script("--deploy", which=have_railway_cmd)
        self.assertEqual(code, 0, err)
        self.assertEqual(runner.calls[-1][0][0], RAILWAY_CMD)
        self.assertIn("railway up", out)
        self.assertNotIn("railway.cmd", out)

    def test_a_failing_build_stops_the_run_with_its_exit_code(self):
        code, _, err, runner = self.run_script("--deploy", runner=FakeRunner(fail_on="build"))
        self.assertEqual(code, 3)
        self.assertEqual(runner.words(), ["build"])
        self.assertIn("`docker` failed", err)
        self.assertFalse(self.stage.exists())

    def test_a_failing_create_stops_before_any_copy_or_removal(self):
        code, _, _, runner = self.run_script(runner=FakeRunner(fail_on="create"))
        self.assertEqual(code, 3)
        self.assertEqual(len(runner.calls), 2)

    def test_a_failing_copy_still_removes_the_container_and_uploads_nothing(self):
        code, _, _, runner = self.run_script("--deploy", runner=FakeRunner(fail_on="cp"))
        self.assertEqual(code, 3)
        self.assertEqual(runner.words(), ["build", "create", "cp", "rm"])
        self.assertFalse((self.stage / "Dockerfile").exists())

    def test_a_failing_removal_is_reported_but_does_not_stop_the_run(self):
        code, _, err, runner = self.run_script("--deploy", runner=FakeRunner(fail_on="rm"))
        self.assertEqual(code, 0)
        self.assertIn("`docker` failed", err)
        self.assertEqual(len(runner.calls), 5)

    def test_a_program_that_cannot_start_ends_the_run_with_a_message_not_a_traceback(self):
        code, _, err, runner = self.run_script("--deploy", which=have_railway_cmd, runner=FakeRunner(cannot_start=RAILWAY_CMD))
        self.assertEqual(code, 2)
        self.assertIn("cannot start", err)
        self.assertIn(RAILWAY_CMD, err)
        self.assertEqual(len(runner.calls), 5)

    def test_a_build_that_left_no_binary_ends_the_run_before_the_upload(self):
        code, _, err, runner = self.run_script("--deploy", runner=FakeRunner(out_files={}))
        self.assertEqual(code, 2)
        self.assertIn("room-server", err)
        self.assertEqual(len(runner.calls), 4)
        self.assertFalse((self.stage / "Dockerfile").exists())


class RuntimeDockerfileTest(unittest.TestCase):
    """Dockerfile.prebuilt must run the binary the way the last stage of Dockerfile runs the one it builds."""

    @staticmethod
    def last_stage(path: Path) -> str:
        return re.split(r"^FROM ", path.read_text(encoding="utf-8"), flags=re.MULTILINE)[-1]

    @staticmethod
    def instructions(stage: str, keyword: str) -> list[str]:
        return re.findall(rf"^{keyword}\s+(.+)$", stage, flags=re.MULTILINE)

    def setUp(self):
        self.prebuilt = self.last_stage(build_and_deploy.RUNTIME_DOCKERFILE)
        self.original = self.last_stage(build_and_deploy.BUILD_DOCKERFILE)

    def test_it_copies_only_the_staged_app_folder(self):
        self.assertEqual(self.instructions(self.prebuilt, "COPY"), [f"{build_and_deploy.APP_FOLDER}/ /app/"])
        self.assertNotIn("cmake", self.prebuilt)

    def test_it_has_the_same_base_user_settings_and_entrypoint_as_the_image_built_from_source(self):
        for keyword in ("ENV", "EXPOSE", "USER", "ENTRYPOINT", "WORKDIR"):
            self.assertEqual(self.instructions(self.prebuilt, keyword), self.instructions(self.original, keyword), keyword)
        self.assertEqual(self.instructions(self.prebuilt, "USER"), ["nobody"])
        self.assertEqual(self.instructions(self.prebuilt, "CMD"), [])
        self.assertRegex(self.prebuilt, r"(?m)^debian:[\w.-]*slim")
        self.assertIn("libstdc++6", self.prebuilt)
        self.assertIn(f"/app/{build_and_deploy.BINARY_NAME}", self.instructions(self.prebuilt, "ENTRYPOINT")[0])

    def test_the_build_stage_the_script_targets_and_collects_exists_in_the_dockerfile(self):
        text = build_and_deploy.BUILD_DOCKERFILE.read_text(encoding="utf-8")
        self.assertRegex(text, rf"(?m)^FROM \S+ AS {build_and_deploy.BUILD_STAGE}$")
        self.assertIn(f"/out/{build_and_deploy.BINARY_NAME}", text)
        self.assertEqual(build_and_deploy.OUT_FOLDER, "/out")
        for argument in ("SERVER_TARGET", "PRESET"):
            self.assertIn(f"ARG {argument}=", text)

    def test_no_secret_is_written_in_the_deploy_files(self):
        for name in ("build_and_deploy.py", "Dockerfile.prebuilt"):
            text = (TOOLS_ROOMS / name).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"(?i)(token|password)\s*[=:]\s*[\"'][^\"']+[\"']", name)


if __name__ == "__main__":
    unittest.main()
