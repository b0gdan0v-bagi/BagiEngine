#!/usr/bin/env python3
"""Starts a game, asks it for its `schema` over the control API and writes the JSON (the input of schema_to_xsd.py).

    export_schema.py <path of a built game> [--out schema.json]
    export_schema.py --preset macos-debug --game BagiStress [--root <repository>] [--out schema.json]

The game runs --headless on the manual clock, as in the smoke tests, and is asked to quit when it has answered. Without --out the JSON goes
to stdout. Exit code: 0 written, 1 the game did not start or did not answer, 2 a bad command line or a schema of another version,
77 the executable does not exist (ctest reads 77 as "skipped"). Standard library only.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any

SCHEMA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCHEMA_DIR))
sys.path.insert(0, str(SCHEMA_DIR.parent / "control"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port, preset_executable  # noqa: E402
from schema_common import EXIT_BAD_USAGE, EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, SCHEMA_METHOD, SCHEMA_VERSION  # noqa: E402

JSON_INDENT = 2


class ExportError(Exception):
    """The schema could not be had; the message says why."""


class SchemaVersionError(ExportError):
    """The game answered a schema of another version."""


def fetch_schema(client: BagiControlClient) -> dict[str, Any]:
    """The answer of the `schema` method, checked to be the version this tool reads."""
    try:
        schema = client.call(SCHEMA_METHOD)
    except ControlError as error:
        raise ExportError(f"the game refused '{SCHEMA_METHOD}': {error.code}: {error}") from error
    if not isinstance(schema, dict) or schema.get("version") != SCHEMA_VERSION:
        version = schema.get("version") if isinstance(schema, dict) else None
        raise SchemaVersionError(f"the schema has version {version!r}, this tool reads {SCHEMA_VERSION}")
    return schema


def export_schema(exe: Path, startup_timeout: float = DEFAULT_STARTUP_TIMEOUT) -> dict[str, Any]:
    """Runs the game `exe` and gives its schema; the game is quit (and killed when it does not leave)."""
    port = free_port()
    process = GameProcess(build_game_command(exe, port), cwd=exe.parent)
    try:
        process.start()
        client = process.connect(port, startup_timeout)
        try:
            schema = fetch_schema(client)
            client.call("quit")
        finally:
            try:
                client.close()
            except (OSError, ConnectionClosed):
                pass
        code = process.stop(DEFAULT_SHUTDOWN_TIMEOUT)
        if code != 0:
            raise ExportError(f"the game exited with code {code} after quit")
        return schema
    except (GameLaunchError, ConnectionClosed, OSError) as error:
        raise ExportError(f"{error}\n--- game log (tail) ---\n{process.log_tail()}") from error
    finally:
        process.stop(0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", nargs="?", type=Path, help="the game executable (or --preset and --game)")
    parser.add_argument("--preset", help="the CMake preset that built the game: the executable is build/<preset>/bin/<game>")
    parser.add_argument("--game", help="the executable name for --preset, e.g. BagiStress")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the engine's)")
    parser.add_argument("--out", type=Path, help="the file to write (default: stdout)")
    parser.add_argument("--startup-timeout", type=float, default=DEFAULT_STARTUP_TIMEOUT, help="seconds to wait for the control API")
    args = parser.parse_args()

    by_preset = args.preset is not None or args.game is not None
    if (args.exe is not None) == by_preset or (by_preset and (args.preset is None or args.game is None)):
        parser.error("give the executable either as a path, or as --preset together with --game")
    exe = args.exe if args.exe is not None else preset_executable(args.preset, args.game, args.root)
    if not exe.is_file():
        print(f"SKIPPED: {exe} does not exist")
        return EXIT_SKIPPED

    try:
        schema = export_schema(exe.resolve(), args.startup_timeout)
    except ExportError as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return EXIT_BAD_USAGE if isinstance(error, SchemaVersionError) else EXIT_FAILED
    text = json.dumps(schema, indent=JSON_INDENT, ensure_ascii=False) + "\n"
    if args.out is None:
        sys.stdout.write(text)
    else:
        args.out.write_text(text, encoding="utf-8")
        print(f"wrote {args.out}: {len(schema.get('types', []))} types")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
