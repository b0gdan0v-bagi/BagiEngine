"""Tests of tools/rooms/docker_check.py without docker: the `docker` calls go to a fake runner. Run: python3 -m unittest discover tools/rooms/tests"""

import contextlib
import io
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

# tools/rooms, where docker_check.py and smoke.py are (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import docker_check


class FakeDocker:
    """Records the docker calls; `info_code` and `run_code` are the exit codes of `docker info` and `docker run`, every other call succeeds."""

    def __init__(self, info_code: int = 0, run_code: int = 0):
        self.calls: list[list[str]] = []
        self.info_code = info_code
        self.run_code = run_code

    def __call__(self, arguments, capture=True):
        self.calls.append(list(arguments))
        code = {"info": self.info_code, "run": self.run_code}.get(arguments[0], 0)
        return SimpleNamespace(returncode=code, stdout="", stderr="no such image" if code else "")

    def verbs(self) -> list[str]:
        return [call[0] for call in self.calls]


def have_docker(name: str) -> str | None:
    return "/usr/bin/docker" if name == "docker" else None


def no_docker(name: str) -> None:
    return None


def run_check(argv, runner, which=have_docker) -> tuple[int, str]:
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = docker_check.run(argv, runner=runner, which=which)
    return code, out.getvalue()


class SkipTest(unittest.TestCase):
    def test_without_docker_the_check_is_skipped_and_nothing_is_called(self):
        runner = FakeDocker()
        code, text = run_check([], runner, which=no_docker)
        self.assertEqual(code, docker_check.EXIT_SKIPPED)
        self.assertIn("SKIPPED", text)
        self.assertEqual(runner.calls, [])

    def test_a_daemon_that_does_not_answer_is_a_skip_not_a_failure(self):
        runner = FakeDocker(info_code=1)
        code, _ = run_check([], runner)
        self.assertEqual(code, docker_check.EXIT_SKIPPED)
        self.assertEqual(runner.verbs(), ["info"])

    def test_a_docker_that_cannot_be_started_is_a_skip(self):
        def broken(arguments, capture=True):
            raise FileNotFoundError(2, "no such file")

        code, _ = run_check([], broken)
        self.assertEqual(code, docker_check.EXIT_SKIPPED)


class RunTest(unittest.TestCase):
    def test_a_failed_docker_run_fails_the_check_and_still_removes_the_container(self):
        runner = FakeDocker(run_code=125)
        code, text = run_check(["--image", "some-image"], runner)
        self.assertEqual(code, docker_check.EXIT_FAILED)
        self.assertIn("docker run failed", text)
        self.assertNotIn("build", runner.verbs())  # --image: no build
        self.assertEqual(runner.calls[-1][:2], ["rm", "-f"])

    def test_without_an_image_the_dockerfile_is_built_from_the_engine_root(self):
        runner = FakeDocker(run_code=125)
        run_check([], runner)
        build = next(call for call in runner.calls if call[0] == "build")
        self.assertEqual(build[1:3], ["-f", str(docker_check.ENGINE_ROOT / "tools/rooms/Dockerfile")])
        self.assertEqual(build[-1], str(docker_check.ENGINE_ROOT))

    def test_a_failed_build_fails_the_check(self):
        class FailingBuild(FakeDocker):
            def __call__(self, arguments, capture=True):
                result = super().__call__(arguments, capture)
                if arguments[0] == "build":
                    result.returncode = 1
                return result

        runner = FailingBuild()
        code, text = run_check([], runner)
        self.assertEqual(code, docker_check.EXIT_FAILED)
        self.assertIn("docker build failed", text)
        self.assertNotIn("run", runner.verbs())

    def test_the_container_gets_the_platform_variables_and_a_published_loopback_port(self):
        runner = FakeDocker(run_code=125)
        run_check(["--image", "img"], runner)
        started = next(call for call in runner.calls if call[0] == "run")
        self.assertIn(f"PORT={docker_check.CONTAINER_PORT}", started)
        self.assertIn("BAGI_ROOMS_TEST_GAMES=1", started)
        publish = started[started.index("-p") + 1]
        self.assertTrue(publish.startswith("127.0.0.1:") and publish.endswith(f":{docker_check.CONTAINER_PORT}"), publish)
        self.assertEqual(started[-1], "img")


class PureTest(unittest.TestCase):
    def test_the_listening_port_is_read_from_the_server_line(self):
        self.assertEqual(docker_check.listening_port("x\nBagiRoomServer listening on ws://0.0.0.0:9090\n"), 9090)
        self.assertIsNone(docker_check.listening_port("Room server settings: port 9090"))

    def test_the_container_name_has_the_check_prefix(self):
        self.assertTrue(docker_check.container_name().startswith("bagi-room-check-"))

    def test_a_free_port_is_a_valid_port(self):
        self.assertTrue(0 < docker_check.pick_free_port() < 65536)

    def test_the_arguments_default_to_the_engine_dockerfile(self):
        arguments = docker_check.parse_arguments([])
        self.assertIsNone(arguments.image)
        self.assertEqual(arguments.dockerfile, docker_check.ENGINE_ROOT / "tools/rooms/Dockerfile")
        game = docker_check.parse_arguments(["--context", "/game", "--dockerfile", "/game/engine/tools/rooms/Dockerfile"])
        self.assertEqual(game.dockerfile, Path("/game/engine/tools/rooms/Dockerfile"))


if __name__ == "__main__":
    unittest.main()
