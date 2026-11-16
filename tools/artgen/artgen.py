#!/usr/bin/env python3
"""
The art generation service on the command line (tools/artgen/CLAUDE.md). Standard library only.

    python3 tools/artgen/artgen.py status   [--generator comfyui|fake] [--url URL] [--json]
    python3 tools/artgen/artgen.py presets  --game-root <game repository> [--json]
    python3 tools/artgen/artgen.py generate --game-root <game repository> --preset <name or file> --subject "white knight"
                                            [--count 4] [--seed N] [--out <folder>] [--name <stem>] [--json]

`status` exits 0 when the generator is ready, 1 when it is installed but not running or not ready, 2 when it is not
installed (the editor offers setup then: entry `artgen-setup-pipelines`). `generate` writes `<stem>_<seed>.png` and its
`<stem>_<seed>.artgen.json` (the provenance) per image into `--out` (default `<game root>/artgen/out`); Ctrl+C cancels the
run on the generator and keeps the images already written.
"""

import argparse
import json
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import artgen_preset  # noqa: E402
import artgen_service  # noqa: E402
from artgen_generators import Cancelled, GenerationError, GeneratorState, make_generator  # noqa: E402

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_NOT_INSTALLED = 2
EXIT_CANCELLED = 130


def cmd_status(args):
    status = make_generator(args.generator, args.url).status()
    if args.json:
        print(json.dumps(status.to_json(), indent=2))
    else:
        print(f"{args.generator}: {status.state}" + (f" ({status.reason})" if status.reason else ""))
    if status.state == GeneratorState.READY:
        return EXIT_OK
    return EXIT_NOT_INSTALLED if status.state == GeneratorState.NOT_INSTALLED else EXIT_FAILED


def cmd_presets(args):
    found = artgen_preset.find_presets(args.game_root)
    if args.json:
        print(json.dumps({
            name: ({"error": str(p)} if isinstance(p, Exception) else
                   {"title": p.title, "workflow": p.workflow.name, "size": [p.width, p.height] if p.width else None})
            for name, p in found.items()
        }, indent=2))
    else:
        if not found:
            print(f"no presets in {Path(args.game_root) / 'artgen' / 'presets'}")
        for name, preset in found.items():
            print(f"{name}: BROKEN: {preset}" if isinstance(preset, Exception) else f"{name}: {preset.title} ({preset.workflow.name})")
    return EXIT_FAILED if any(isinstance(p, Exception) for p in found.values()) else EXIT_OK


def load_named_preset(game_root, preset):
    path = Path(preset)
    if path.suffix != ".json":
        path = Path(game_root) / "artgen" / "presets" / f"{preset}.json"
    return artgen_preset.load_preset(path, game_root)


def cmd_generate(args):
    preset = load_named_preset(args.game_root, args.preset)
    out_dir = Path(args.out) if args.out else Path(args.game_root) / "artgen" / "out"
    request = artgen_service.Request(preset, args.subject, args.count, args.seed, out_dir, args.name)
    cancel = threading.Event()

    def progress(event):
        if args.json:
            print(json.dumps(event), flush=True)
        elif event["kind"] == "image":
            print(f"[{event['image'] + 1}/{event['count']}] {event['path']}", flush=True)
        elif event["step"] == event["steps"] or event["step"] == 1:
            print(f"[{event['image'] + 1}/{event['count']}] seed {event['seed']}: step {event['step']}/{event['steps']}", flush=True)

    try:
        results = artgen_service.generate(request, make_generator(args.generator, args.url), progress, cancel)
    except KeyboardInterrupt:
        cancel.set()
        print("cancelled", file=sys.stderr)
        return EXIT_CANCELLED
    except Cancelled:
        print("cancelled", file=sys.stderr)
        return EXIT_CANCELLED
    if args.json:
        print(json.dumps({"kind": "done", "images": [{"path": str(r.path), "provenance": str(r.provenance_path), "seed": r.seed} for r in results]}))
    return EXIT_OK


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("status", "presets", "generate"):
        p = sub.add_parser(name)
        p.add_argument("--json", action="store_true")
        if name != "presets":
            p.add_argument("--generator", default="comfyui", choices=["comfyui", "fake"])
            p.add_argument("--url", help="the ComfyUI address (default: COMFYUI_URL, local.json, the local one)")
        if name != "status":
            p.add_argument("--game-root", required=True, help="the game repository (its artgen/presets/)")
        if name == "generate":
            p.add_argument("--preset", required=True, help="a preset name of the game, or a .json file")
            p.add_argument("--subject", required=True)
            p.add_argument("--count", type=int, default=4)
            p.add_argument("--seed", type=int)
            p.add_argument("--out")
            p.add_argument("--name", default="")
    args = parser.parse_args(argv)
    try:
        return {"status": cmd_status, "presets": cmd_presets, "generate": cmd_generate}[args.command](args)
    except (artgen_preset.PresetError, artgen_service.RequestError, GenerationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
