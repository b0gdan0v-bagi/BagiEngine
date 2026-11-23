"""Tests of tools/rooms/load.py without a build: the server and the probe are fakes. Run: python3 -m unittest discover tools/rooms/tests"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

# tools/rooms, where load.py and smoke.py are (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import load
import smoke

PORT = 4242
REFUSAL = "BagiRoomServer: Profiler: the profiler is not in this build: configure with ENGINE_PROFILER=ON (preset macos-profile)"


class FakeServer:
    """Stands for RoomServer: records its command, writes its trace when asked to and ends with `exit_code`."""

    instances: list["FakeServer"] = []
    refusal: str | None = None
    write_trace = True
    exit_code = 0

    def __init__(self, exe: Path, extra_args: list[str]):
        self.command = [str(exe), "--test-games", "--port", "0", "--computer-delay-ms", "0", "--bind", "loopback", *extra_args]
        self.port = PORT
        self.events: list[str] = []
        FakeServer.instances.append(self)

    def __enter__(self) -> "FakeServer":
        if FakeServer.refusal is not None:
            raise smoke.ServerExitedEarly(1, FakeServer.refusal, FakeServer.refusal)
        self.events.append("listening")
        return self

    def __exit__(self, *rest: object) -> None:
        return None

    def terminate(self) -> None:
        self.events.append("terminate")

    def wait_for_exit(self, limit: float) -> int:
        self.events.append(f"wait {limit:g}")
        trace = next(argument for argument in self.command if argument.startswith("--trace="))
        if FakeServer.write_trace:
            Path(trace.removeprefix("--trace=")).write_text("{}")
        return FakeServer.exit_code

    def output(self) -> str:
        return "server log line"


class FakeProbe:
    """Stands for the probe process: records its command and writes its trace when asked to."""

    def __init__(self, code: int = 0, output: str = "PROBE LOAD OK clients=4 actions=160 hands=3 seconds=9.5", write_trace: bool = True):
        self.code = code
        self.output = output
        self.write_trace = write_trace
        self.calls: list[tuple[list[str], float]] = []

    def __call__(self, command: list[str], cwd: Path, limit: float) -> tuple[int, str]:
        self.calls.append((command, limit))
        if self.write_trace:
            trace = next(argument for argument in command if argument.startswith("--trace="))
            Path(trace.removeprefix("--trace=")).write_text("{}")
        return self.code, self.output


class Summaries:
    def __init__(self) -> None:
        self.calls: list[tuple[str, int]] = []

    def __call__(self, path: Path, top: int) -> str:
        self.calls.append((path.name, top))
        return f"summary of {path.name}"


class LoadTest(unittest.TestCase):
    def setUp(self) -> None:
        FakeServer.instances = []
        FakeServer.refusal = None
        FakeServer.write_trace = True
        FakeServer.exit_code = 0
        self._build = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(self._build.cleanup)
        self.root = Path(self._build.name)
        self.out = self.root / "out"
        bin_dir = self.root / "build" / "test-preset" / "bin"
        bin_dir.mkdir(parents=True)
        for name in (smoke.SERVER_NAME, smoke.PROBE_EXE_NAME):
            (bin_dir / name).write_text("")

    def run_load(self, *extra: str, probe: FakeProbe | None = None, summaries: Summaries | None = None) -> tuple[int, str]:
        out = io.StringIO()
        argv = ["--preset", "test-preset", "--root", str(self.root), "--out", str(self.out), *extra]
        with contextlib.redirect_stdout(out):
            code = load.run(argv, make_server=FakeServer, run_probe=probe or FakeProbe(), summarize_text=summaries or Summaries())
        return code, out.getvalue()

    def test_the_server_runs_without_limits_and_with_a_trace_and_the_probe_gets_its_port(self) -> None:
        probe = FakeProbe()
        code, _ = self.run_load("--clients", "16", "--messages", "200", probe=probe)
        self.assertEqual(code, load.EXIT_PASSED)
        server = FakeServer.instances[0].command
        self.assertEqual(server[server.index("--max-per-address") + 1], "0")
        self.assertEqual(server[server.index("--computer-delay-ms") + 1], "0")
        self.assertIn(f"--trace={self.out / load.SERVER_TRACE}", server)
        command, limit = probe.calls[0]
        self.assertEqual(command[command.index("--url") + 1], f"ws://127.0.0.1:{PORT}")
        self.assertEqual(command[command.index("--clients") + 1], "16")
        self.assertEqual(command[command.index("--messages") + 1], "200")
        self.assertIn(f"--trace={self.out / load.PROBE_TRACE}", command)
        self.assertGreater(limit, 200 * load.PROBE_SECONDS_PER_MESSAGE)

    def test_the_server_ends_with_sigterm_in_ten_seconds_after_the_probe(self) -> None:
        self.run_load()
        self.assertEqual(FakeServer.instances[0].events, ["listening", "terminate", f"wait {smoke.SHUTDOWN_SECONDS:g}"])

    def test_both_summaries_are_printed_in_order_with_top(self) -> None:
        summaries = Summaries()
        code, text = self.run_load("--top", "7", summaries=summaries)
        self.assertEqual(code, load.EXIT_PASSED)
        self.assertEqual(summaries.calls, [(load.SERVER_TRACE, 7), (load.PROBE_TRACE, 7)])
        self.assertLess(text.index("summary of server.json"), text.index("summary of probe.json"))
        self.assertIn("PROBE LOAD OK", text)

    def test_a_server_that_refuses_the_trace_is_a_skip(self) -> None:
        FakeServer.refusal = REFUSAL
        probe = FakeProbe()
        code, text = self.run_load(probe=probe)
        self.assertEqual(code, load.EXIT_SKIPPED)
        self.assertIn("no profiler", text)
        self.assertEqual(probe.calls, [])

    def test_a_server_that_ends_early_for_another_reason_is_a_failure(self) -> None:
        FakeServer.refusal = "BagiRoomServer: unknown flag --nope"
        self.assertEqual(self.run_load()[0], load.EXIT_FAILED)

    def test_a_missing_executable_is_a_skip(self) -> None:
        (self.root / "build" / "test-preset" / "bin" / smoke.PROBE_EXE_NAME).unlink()
        self.assertEqual(self.run_load()[0], load.EXIT_SKIPPED)

    def test_a_failing_probe_is_a_failure_and_the_server_is_still_stopped(self) -> None:
        summaries = Summaries()
        code, text = self.run_load(probe=FakeProbe(code=1, output="PROBE FAILED client 2: rate_limited"), summaries=summaries)
        self.assertEqual(code, load.EXIT_FAILED)
        self.assertIn("PROBE FAILED client 2", text)
        self.assertEqual(FakeServer.instances[0].events[-2], "terminate")
        self.assertEqual(summaries.calls, [])

    def test_a_probe_that_exits_0_without_its_line_is_a_failure(self) -> None:
        self.assertEqual(self.run_load(probe=FakeProbe(output="nothing"))[0], load.EXIT_FAILED)

    def test_a_missing_server_trace_is_a_failure(self) -> None:
        FakeServer.write_trace = False
        code, text = self.run_load()
        self.assertEqual(code, load.EXIT_FAILED)
        self.assertIn("server wrote no trace", text)

    def test_a_missing_probe_trace_is_a_failure(self) -> None:
        code, text = self.run_load(probe=FakeProbe(write_trace=False))
        self.assertEqual(code, load.EXIT_FAILED)
        self.assertIn("probe wrote no trace", text)

    def test_an_empty_trace_is_a_failure(self) -> None:
        probe = FakeProbe(write_trace=False)
        self.out.mkdir()
        (self.out / load.PROBE_TRACE).write_text("")
        self.assertEqual(self.run_load(probe=probe)[0], load.EXIT_FAILED)

    @unittest.skipIf(sys.platform == "win32", "a terminated process has no exit code to judge")
    def test_a_server_that_does_not_exit_0_is_a_failure(self) -> None:
        FakeServer.exit_code = 3
        self.assertEqual(self.run_load()[0], load.EXIT_FAILED)


class ArgumentTest(unittest.TestCase):
    def test_the_numbers_are_bounded_like_the_probe(self) -> None:
        for argv in (["--clients", "0"], ["--clients", str(load.MAX_CLIENTS + 1)], ["--messages", "0"], ["--messages", str(load.MAX_MESSAGES + 1)]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                load.parse_args(["--preset", "p", *argv])


if __name__ == "__main__":
    unittest.main()
