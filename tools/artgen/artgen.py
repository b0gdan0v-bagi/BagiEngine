#!/usr/bin/env python3
"""
The art generation service on the command line (tools/artgen/CLAUDE.md). Standard library only.

    python3 tools/artgen/artgen.py status   [--generator comfyui|fake] [--url URL] [--json]
    python3 tools/artgen/artgen.py presets  --game-root <game repository> [--json]
    python3 tools/artgen/artgen.py generate --game-root <game repository> --preset <name or file> --subject "white knight"
                                            [--count 4] [--seed N] [--out <folder>] [--name <stem>] [--json]
                                            [--min-pass N] [--extra N]
                                            [--reference-image <path> [--reference-mode init] [--reference-weight 0.4]]
    python3 tools/artgen/artgen.py check    <png>... [--preset <name or file> [--game-root <game repository>]] [--json]

`check` runs the cut-out checks (artgen_checks.py) on files already on disk: exit 0 when every file passes or has no alpha
channel, 1 when one fails or cannot be read, 2 for a file or preset that cannot be opened.

`generate` makes up to `--extra` more images (on the next seeds) while fewer than `--min-pass` pass their checks; both default to
the preset's `regenerate`, and a run still short at the limit exits 0 (the verdicts are in the provenance).

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

import artgen_checks  # noqa: E402
import artgen_preset  # noqa: E402
import artgen_service  # noqa: E402
from artgen_generators import Cancelled, GenerationError, GeneratorState, make_generator  # noqa: E402

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_NOT_INSTALLED = 2
EXIT_USAGE = 2  # `check`: a file or a preset that cannot be read
EXIT_CANCELLED = 130
DEFAULT_REFERENCE_MODE = "init"


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
            name: ({"error": str(p), "source": getattr(p, "source", None)} if isinstance(p, Exception) else
                   {"title": p.title, "source": p.source, "workflow": p.workflow.name, "size": [p.width, p.height] if p.width else None})
            for name, p in found.items()
        }, indent=2))
    else:
        if not found:
            print(f"no presets in {Path(args.game_root) / 'artgen' / 'presets'}")
        for name, preset in found.items():
            print(f"{name}: BROKEN: {preset}" if isinstance(preset, Exception) else f"{name}: {preset.title} ({preset.workflow.name}, {preset.source})")
    return EXIT_FAILED if any(isinstance(p, Exception) for p in found.values()) else EXIT_OK


def load_named_preset(game_root, preset):
    path = Path(preset)
    if path.suffix == ".json":
        return artgen_preset.load_preset(path, game_root)
    return artgen_preset.find_preset(preset, game_root)


def reference_of_args(args):
    """The request's reference from the flags: the image alone means `init`, the weight is left to the mode's default."""
    if args.reference_image is None:
        if args.reference_mode or args.reference_weight is not None:
            raise artgen_service.RequestError("--reference-mode and --reference-weight need --reference-image")
        return None
    reference = {"mode": args.reference_mode or DEFAULT_REFERENCE_MODE, "image": args.reference_image}
    if args.reference_weight is not None:
        reference["weight"] = args.reference_weight
    return reference


def cmd_generate(args):
    preset = load_named_preset(args.game_root, args.preset)
    out_dir = Path(args.out) if args.out else Path(args.game_root) / "artgen" / "out"
    request = artgen_service.Request(
        preset, args.subject, args.count, args.seed, out_dir, args.name, reference_of_args(args), Path(args.game_root),
        min_pass=args.min_pass, extra=args.extra,
    )
    cancel = threading.Event()

    def progress(event):
        if args.json:
            print(json.dumps(event), flush=True)
        elif event["kind"] == "image":
            print(f"[{event['image'] + 1}/{event['count']}] {event['path']}" + (" (extra)" if event.get("extra") else ""), flush=True)
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
    passing = artgen_service.count_passing(results)
    if args.json:
        images = [{"path": str(r.path), "provenance": str(r.provenance_path), "seed": r.seed, "extra": r.extra,
                   "passed": r.provenance.get("checks", {}).get("passed")} for r in results]
        print(json.dumps({"kind": "done", "images": images, "passing": passing}))
    else:
        wanted, _ = artgen_service.regeneration_of(request)
        if passing < wanted:
            print(f"only {passing} of the {wanted} wanted images pass their checks", flush=True)
    return EXIT_OK


def check_line(path, verdict):
    if verdict is None:
        return f"{path}: not applicable (no alpha channel)"
    if verdict["passed"] is None:
        return f"{path}: cannot be read: {verdict['error']}"
    return f"{path}: passed" if verdict["passed"] else f"{path}: FAILED {', '.join(verdict['failed'])}"


def cmd_check(args):
    """The verdict of each PNG with the thresholds of the named preset (the defaults when it names none, or has the checks off: the
    person asked for them here). A picture that cannot be read counts as a failure, one without alpha does not."""
    thresholds = None
    if args.preset:
        thresholds = load_named_preset(args.game_root, args.preset).checks
    results = []
    for name in args.png:
        try:
            data = Path(name).read_bytes()
        except OSError as error:
            print(f"error: {error}", file=sys.stderr)
            return EXIT_USAGE
        results.append((name, artgen_checks.check_png(data, thresholds)))
    if args.json:
        print(json.dumps([{"path": name, "checks": verdict} for name, verdict in results], indent=2))
    else:
        for name, verdict in results:
            print(check_line(name, verdict))
    return EXIT_FAILED if any(v is not None and not v["passed"] for _, v in results) else EXIT_OK


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
            p.add_argument("--min-pass", type=int, help="how many images must pass their checks (default: the preset's regenerate.minPass, else --count)")
            p.add_argument("--extra", type=int, help="the most extra images made when too few pass (default: the preset's regenerate.extra, else 0)")
            p.add_argument("--reference-image", help="a picture to start from: under the game root, or an absolute path")
            p.add_argument("--reference-mode", help=f"how the picture is used (default {DEFAULT_REFERENCE_MODE})")
            p.add_argument("--reference-weight", type=float, help="how strongly the picture holds, above 0 and below 1 (higher keeps more)")
    check = sub.add_parser("check", help="run the checks of a cut-out on PNG files")
    check.add_argument("png", nargs="+")
    check.add_argument("--preset", help="a preset name or .json file whose thresholds are used (default: the defaults)")
    check.add_argument("--game-root", help="the game repository, to find the preset")
    check.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        return {"status": cmd_status, "presets": cmd_presets, "generate": cmd_generate, "check": cmd_check}[args.command](args)
    except (artgen_preset.PresetError, artgen_service.RequestError, GenerationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE if args.command == "check" else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
