#!/usr/bin/env python3
"""
Turn TODO.md into data, a dashboard and tracker tasks.

Commands:
  check      Parse TODO.md and print one line per entry (exit 1 on a format error).
  parse      Print the parsed TODO.md as JSON (or write it with --out).
  dashboard  Write a self-contained HTML page (default: build/todo/index.html).
  push       Export the entries to a Vikunja project (one-way; --dry-run changes nothing).
             Settings: VIKUNJA_URL, VIKUNJA_TOKEN, VIKUNJA_PROJECT in the environment or in .env.

Run from the project root (or pass --todo). Plan paths and .env are looked up in --root,
which defaults to the folder of the TODO file, so nothing here names a concrete game.
"""

import argparse
import json
import sys
from datetime import date
from pathlib import Path

from dashboard import render_dashboard
from todo_parser import ParseError, parse_todo
from vikunja import VikunjaClient, VikunjaError, load_settings, sync

DEFAULT_TODO = "TODO.md"
DEFAULT_DASHBOARD_DIR = "build/todo"
DASHBOARD_FILE = "index.html"
ENV_FILE_NAME = ".env"


def root_of(args: argparse.Namespace) -> Path:
    return Path(args.root) if args.root else Path(args.todo).resolve().parent


def load(args: argparse.Namespace) -> dict:
    data = parse_todo(Path(args.todo), root_of(args))
    for warning in data["warnings"]:
        print(f"[warn] {warning}", file=sys.stderr)
    return data


def cmd_check(args: argparse.Namespace) -> int:
    data = load(args)
    for entry in data["entries"]:
        progress = entry["planProgress"]
        progress_text = f"{progress['done']}/{progress['total']}" if progress else "-"
        print(
            f"{entry['stage'] or '-':>2}  {entry['status']:<11}  {entry['date'] or '-':<10}  "
            f"{progress_text:>6}  {entry['id']}"
        )
    print(f"{len(data['entries'])} entries, {len(data['stages'])} stages, {len(data['warnings'])} warnings")
    return 0


def cmd_parse(args: argparse.Namespace) -> int:
    text = json.dumps(load(args), ensure_ascii=False, indent=2) + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


def cmd_dashboard(args: argparse.Namespace) -> int:
    page = render_dashboard(load(args), date.today())
    out_dir = Path(args.out or DEFAULT_DASHBOARD_DIR)
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / DASHBOARD_FILE
    target.write_text(page, encoding="utf-8")
    print(target.resolve())
    return 0


def cmd_push(args: argparse.Namespace) -> int:
    data = load(args)
    env_file = Path(args.env_file) if args.env_file else root_of(args) / ENV_FILE_NAME
    settings = load_settings(env_file=env_file, project=args.project)
    report = sync(VikunjaClient(settings.url, settings.token), data, settings.project, dry_run=args.dry_run)
    for line in report.lines:
        print(line)
    for warning in report.warnings:
        print(f"[warn] {warning}", file=sys.stderr)
    prefix = "dry run, nothing was changed: " if args.dry_run else ""
    print(f"{prefix}{report.summary()} in project '{settings.project}'")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--todo", default=DEFAULT_TODO, help=f"TODO file (default: {DEFAULT_TODO})")
    parser.add_argument("--root", help="folder that plan paths are relative to (default: the TODO file's folder)")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("check", help="parse and print one line per entry").set_defaults(run=cmd_check)

    parse = commands.add_parser("parse", help="print the parsed TODO.md as JSON")
    parse.add_argument("--out", help="write the JSON to this file instead of stdout")
    parse.set_defaults(run=cmd_parse)

    dashboard = commands.add_parser("dashboard", help="write the HTML dashboard")
    dashboard.add_argument("--out", help=f"output folder (default: {DEFAULT_DASHBOARD_DIR})")
    dashboard.set_defaults(run=cmd_dashboard)

    push = commands.add_parser("push", help="export the entries to Vikunja")
    push.add_argument("--dry-run", action="store_true", help="read from Vikunja, print what would change, write nothing")
    push.add_argument("--project", help="Vikunja project title (default: VIKUNJA_PROJECT, then BagiEngine)")
    push.add_argument("--env-file", help=f"settings file (default: {ENV_FILE_NAME} in the root)")
    push.set_defaults(run=cmd_push)

    return parser


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):  # a captured stream (pytest) has none
            stream.reconfigure(encoding="utf-8")
    args = build_parser().parse_args()
    try:
        return args.run(args)
    except (ParseError, VikunjaError) as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
