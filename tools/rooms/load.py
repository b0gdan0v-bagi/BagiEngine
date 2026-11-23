#!/usr/bin/env python3
"""A load run of the room server with traces: where do its threads spend time and allocate under many clients? Manual (it needs the profile build).

    python3 tools/rooms/load.py --preset macos-profile [--clients 4] [--messages 40] [--out DIR] [--top 25]

Starts `BagiRoomServer --test-games --port 0 --bind loopback --max-per-address 0 --computer-delay-ms 0 --trace=<out>/server.json`, runs
`BagiRoomClientProbe --clients N --messages M --trace=<out>/probe.json` against its port (N clients in one process, M actions each, about 4 actions
a second per client), ends the server with SIGTERM (it has to exit 0 within 10 s) and prints `tools/perf/trace_summary.py` of both traces: the
zones by self and total time and, in a build with ENGINE_PROFILER_ALLOC, the allocation names and sizes. The traces stay in --out (default
build/rooms-load/<time>/). The server's traces rank the work of `websocket-frame-buffers`; the probe's show the client's side.

Exit code: 0 done, 1 the probe failed, a trace is missing or empty, or the server did not end well, 77 skipped (an executable does not exist, or the
build has no profiler: the server refused --trace). Standard library only, Python 3.10+.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable

from smoke import (  # noqa: E402
    EXIT_FAILED,
    EXIT_PASSED,
    EXIT_SKIPPED,
    PROBE_EXE_NAME,
    PROBE_LOAD_OK,
    PROFILER_REFUSAL,
    SERVER_NAME,
    SHUTDOWN_SECONDS,
    RoomServer,
    ServerExitedEarly,
    SmokeFailure,
    preset_executable,
)
from game_process import REPOSITORY_ROOT  # noqa: E402
from trace_summary import format_text, load_trace, summarize  # noqa: E402

DEFAULT_CLIENTS = 4
DEFAULT_MESSAGES = 40
MAX_CLIENTS = 64  # the probe's own limits
MAX_MESSAGES = 100000
DEFAULT_TOP = 25
OUT_ROOT = Path("build") / "rooms-load"
SERVER_TRACE = "server.json"
PROBE_TRACE = "probe.json"
PROBE_BASE_SECONDS = 60.0
PROBE_SECONDS_PER_MESSAGE = 0.5  # twice what the probe's pacing needs
SERVER_FLAGS = ["--max-per-address", "0"]  # the load comes from one address; --computer-delay-ms 0 is RoomServer's own default

ProbeRunner = Callable[[list[str], Path, float], tuple[int, str]]
Summarizer = Callable[[Path, int], str]


def run_probe_process(command: list[str], cwd: Path, limit: float) -> tuple[int, str]:
    """The exit code and the output (stdout and stderr) of the probe; a probe that outlives `limit` is killed and reported as -1."""
    try:
        done = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=limit, check=False)
    except subprocess.TimeoutExpired as error:
        output = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        return -1, f"{output}\nthe probe did not end within {limit:.0f} s"
    return done.returncode, done.stdout + done.stderr


def summarize_trace(path: Path, top: int) -> str:
    return format_text(summarize(*load_trace(str(path))), top)


def has_content(path: Path) -> bool:
    return path.is_file() and path.stat().st_size > 0


def positive_int(limit: int) -> Callable[[str], int]:
    def parse(text: str) -> int:
        value = int(text)
        if not 1 <= value <= limit:
            raise argparse.ArgumentTypeError(f"{value} is not in 1..{limit}")
        return value

    return parse


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", required=True, help="the CMake preset that built the executables (a profile preset: macos-profile)")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--clients", type=positive_int(MAX_CLIENTS), default=DEFAULT_CLIENTS, help="clients of the probe process")
    parser.add_argument("--messages", type=positive_int(MAX_MESSAGES), default=DEFAULT_MESSAGES, help="actions each client sends")
    parser.add_argument("--out", type=Path, help=f"where the traces go (default {OUT_ROOT}/<time>/ under the root)")
    parser.add_argument("--top", type=int, default=DEFAULT_TOP, help="zones per summary table")
    return parser.parse_args(argv)


def run(
    argv: list[str],
    make_server: Callable[[Path, list[str]], RoomServer] = RoomServer,
    run_probe: ProbeRunner = run_probe_process,
    summarize_text: Summarizer = summarize_trace,
) -> int:
    """The whole run; the three callables are what a unit test replaces."""
    args = parse_args(argv)
    server_exe = preset_executable(args.preset, SERVER_NAME, args.root)
    probe_exe = preset_executable(args.preset, PROBE_EXE_NAME, args.root)
    for exe in (server_exe, probe_exe):
        if not exe.is_file():
            print(f"SKIPPED: {exe} does not exist")
            return EXIT_SKIPPED

    out = args.out or (args.root or REPOSITORY_ROOT) / OUT_ROOT / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    server_trace = out / SERVER_TRACE
    probe_trace = out / PROBE_TRACE

    try:
        with make_server(server_exe.resolve(), [*SERVER_FLAGS, f"--trace={server_trace}"]) as server:
            print(f"server: {' '.join(server.command)}", flush=True)
            command = [
                str(probe_exe.resolve()),
                "--url", f"ws://127.0.0.1:{server.port}",
                "--game", "cards",
                "--clients", str(args.clients),
                "--messages", str(args.messages),
                f"--trace={probe_trace}",
            ]  # fmt: skip
            print(f"probe: {' '.join(command)}", flush=True)
            limit = PROBE_BASE_SECONDS + PROBE_SECONDS_PER_MESSAGE * args.messages
            probe_code, probe_output = run_probe(command, probe_exe.resolve().parent, limit)
            server.terminate()
            server_code = server.wait_for_exit(SHUTDOWN_SECONDS)
            server_log = server.output()
    except ServerExitedEarly as early:
        if PROFILER_REFUSAL in early.output:
            print(f"SKIPPED: the server refused --trace, the build has no profiler (use a profile preset)\n{early.output.strip()}")
            return EXIT_SKIPPED
        print(f"FAILED: {early}")
        return EXIT_FAILED
    except SmokeFailure as error:
        print(f"FAILED: {error}")
        return EXIT_FAILED

    print(probe_output.rstrip())
    if probe_code != 0 or PROBE_LOAD_OK not in probe_output:
        print(f"FAILED: the probe exited with {probe_code}\n--- server log (tail) ---\n{log_tail_of(server_log)}")
        return EXIT_FAILED
    if sys.platform != "win32" and server_code != 0:
        print(f"FAILED: the server exited with code {server_code} after SIGTERM, not 0\n--- server log (tail) ---\n{log_tail_of(server_log)}")
        return EXIT_FAILED
    for who, trace in (("server", server_trace), ("probe", probe_trace)):
        if not has_content(trace):
            print(f"FAILED: the {who} wrote no trace to {trace}")
            return EXIT_FAILED
    for who, trace in (("server", server_trace), ("probe", probe_trace)):
        try:
            text = summarize_text(trace, args.top)
        except (OSError, ValueError, KeyError) as error:
            print(f"FAILED: the trace of the {who} does not parse: {error}")
            return EXIT_FAILED
        print(f"\n=== {who}: {trace} ===\n{text}")
    return EXIT_PASSED


def log_tail_of(text: str, lines: int = 20) -> str:
    return "\n".join(text.splitlines()[-lines:])


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
