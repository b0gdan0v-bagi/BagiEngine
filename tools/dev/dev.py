#!/usr/bin/env python3
"""
Build and run the products of a repository on BagiEngine: the one command line the VS Code panel
`tools/vscode/bagi-dev`, the terminal and the agents share (tools/dev/CLAUDE.md).

    python3 engine/tools/dev/dev.py info [--json]                 # presets, flags, products, chains, tools
    python3 engine/tools/dev/dev.py flags <preset> [--set K=V ...] [--unset K ...] [--json]
    python3 engine/tools/dev/dev.py build <product> [--preset P] [--configure]
    python3 engine/tools/dev/dev.py run <product> [--preset P] [--no-build] [-- extra args]
    python3 engine/tools/dev/dev.py chain <name> [--preset P] [--web-preset W] [--no-build]
    python3 engine/tools/dev/dev.py tool <name> [-- extra args]

The repository describes its products in `dev.json` at its root (what to build, how to start it, the chains of
several, its tools); the presets are its CMakePresets.json with the includes. Flags go into CMakeUserPresets.json as
the presets `<preset>-dev` (configure, build and test) that inherit `<preset>`, so CMake Tools, the CLI and the
agents see the same configuration, and the flagged build has its own folder `build/<preset>-dev/`.
Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import signal
import subprocess
import sys
import time
from pathlib import Path

MANIFEST = "dev.json"
USER_PRESETS = "CMakeUserPresets.json"
DEV_SUFFIX = "-dev"
USER_PRESETS_VERSION = 6

# The engine's switches worth a toggle (cmake/*.cmake and CMakeLists.txt have them all); a manifest's "flags" replace these.
DEFAULT_FLAGS = [
    {"name": "BE_WITH_EDITOR", "label": "editor panels (also in Release)"},
    {"name": "ENGINE_PROFILER", "label": "profiler zones and frames"},
    {"name": "ENGINE_PROFILER_ALLOC", "label": "allocation counting (needs the profiler)"},
    {"name": "ENGINE_PROFILER_TRACY", "label": "Tracy client (needs the profiler)"},
    {"name": "ENGINE_FUZZ", "label": "AddressSanitizer + UBSan on every target"},
    {"name": "ENGINE_SINGLE_THREAD", "label": "task system without worker threads"},
    {"name": "ENGINE_ENABLE_ASSERTS", "label": "ASSERT macros", "default": "ON"},
    {"name": "ENGINE_UI_RMLUI", "label": "RmlUi host module"},
    {"name": "ENGINE_CCACHE", "label": "compile through ccache", "default": "ON"},
]

HOST_PREFIX = {"Darwin": "macos-", "Windows": "windows-", "Linux": "linux-"}


class DevError(Exception):
    pass


# --- the repository, its manifest and its presets ---------------------------------------------------------


def find_root(start: Path) -> Path:
    """The nearest folder upwards with a CMakePresets.json: the repository the commands work on."""
    for folder in [start, *start.parents]:
        if (folder / "CMakePresets.json").is_file():
            return folder
    raise DevError(f"no CMakePresets.json in {start} or above")


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except ValueError as error:
        raise DevError(f"{path}: {error}") from error


def load_manifest(root: Path) -> dict:
    manifest = read_json(root / MANIFEST)
    manifest.setdefault("products", [])
    manifest.setdefault("chains", [])
    manifest.setdefault("tools", [])
    manifest.setdefault("flags", DEFAULT_FLAGS)
    return manifest


def preset_files(root: Path) -> list[tuple[Path, dict]]:
    """CMakePresets.json with its includes (depth first, once each), then CMakeUserPresets.json."""
    seen: set[Path] = set()
    out: list[tuple[Path, dict]] = []

    def visit(path: Path) -> None:
        path = path.resolve()
        if path in seen or not path.is_file():
            return
        seen.add(path)
        data = read_json(path)
        for include in data.get("include", []):
            visit(path.parent / include)
        out.append((path, data))

    visit(root / "CMakePresets.json")
    visit(root / USER_PRESETS)
    return out


def presets_of(root: Path, kind: str) -> dict[str, dict]:
    """Every preset of a kind ("configurePresets", "buildPresets", "testPresets") by name."""
    found: dict[str, dict] = {}
    for _path, data in preset_files(root):
        for preset in data.get(kind, []):
            found[preset["name"]] = preset
    return found


def host_matches(preset: dict, presets: dict[str, dict]) -> bool:
    """False when the preset (or one it inherits) has an `equals` condition on ${hostSystemName} that this host fails."""
    host = platform.system()
    stack = [preset]
    while stack:
        p = stack.pop()
        condition = p.get("condition")
        if isinstance(condition, dict) and condition.get("type") == "equals" and condition.get("lhs") == "${hostSystemName}":
            if condition.get("rhs") != host:
                return False
        parents = p.get("inherits") or []
        stack += [presets[name] for name in ([parents] if isinstance(parents, str) else parents) if name in presets]
    return True


def preset_groups(root: Path) -> dict:
    """The visible configure presets this host can use: desktop (the host's prefix), web, and the user's own."""
    configure = presets_of(root, "configurePresets")
    user = {p["name"] for p in read_json(root / USER_PRESETS).get("configurePresets", [])}
    prefix = HOST_PREFIX.get(platform.system(), "")
    groups: dict[str, list[str]] = {"desktop": [], "web": [], "user": [], "other": []}
    for name, preset in configure.items():
        if preset.get("hidden") or not host_matches(preset, configure):
            continue
        if name in user:
            groups["user"].append(name)
        elif name.startswith("web-"):
            groups["web"].append(name)
        elif prefix and name.startswith(prefix):
            groups["desktop"].append(name)
        else:
            groups["other"].append(name)
    return groups


def kind_of_preset(name: str) -> str:
    base = name[: -len(DEV_SUFFIX)] if name.endswith(DEV_SUFFIX) else name
    return "web" if base.startswith("web-") else "desktop"


def default_preset(root: Path, kind: str, prefer: str = "debug") -> str:
    """`<host>-<prefer>` (`web-<prefer>` for the web; prefer is debug or release), else the first preset of the kind."""
    names = preset_groups(root)["web" if kind == "web" else "desktop"]
    wanted = ("web-" if kind == "web" else HOST_PREFIX.get(platform.system(), "")) + prefer
    if wanted in names:
        return wanted
    if names:
        return names[0]
    raise DevError(f"no {kind} preset for this host in CMakePresets.json")


# --- flags: the user presets <preset>-dev ------------------------------------------------------------------


def dev_presets(root: Path) -> dict[str, dict]:
    """The `<base>-dev` presets of CMakeUserPresets.json: {name: {base, flags}}."""
    out = {}
    for preset in read_json(root / USER_PRESETS).get("configurePresets", []):
        name = preset.get("name", "")
        if name.endswith(DEV_SUFFIX) and preset.get("inherits"):
            out[name] = {"base": preset["inherits"] if isinstance(preset["inherits"], str) else preset["inherits"][0], "flags": preset.get("cacheVariables", {})}
    return out


def set_flags(root: Path, preset: str, sets: dict[str, str], unsets: list[str]) -> dict:
    """Change the flags of `<base>-dev` (`preset` may name the base or the dev preset). No flag left: the three
    presets are removed. Other presets in the file are kept as they are."""
    base = preset[: -len(DEV_SUFFIX)] if preset.endswith(DEV_SUFFIX) else preset
    configure = presets_of(root, "configurePresets")
    if base not in configure:
        raise DevError(f"no configure preset {base}")
    name = base + DEV_SUFFIX
    path = root / USER_PRESETS
    data = read_json(path) or {"version": USER_PRESETS_VERSION}
    data.setdefault("version", USER_PRESETS_VERSION)
    current = next((p for p in data.get("configurePresets", []) if p.get("name") == name), None)
    flags = dict(current.get("cacheVariables", {})) if current else {}
    flags.update(sets)
    for key in unsets:
        flags.pop(key, None)
    for kind in ("configurePresets", "buildPresets", "testPresets"):
        data[kind] = [p for p in data.get(kind, []) if p.get("name") != name]
    if flags:
        data["configurePresets"].append({"name": name, "displayName": f"{base} + {', '.join(f'{k}={v}' for k, v in sorted(flags.items()))}", "inherits": base, "cacheVariables": flags})
        build = presets_of(root, "buildPresets").get(base)
        data["buildPresets"].append({"name": name, **({"inherits": base} if build else {}), "configurePreset": name})
        if base in presets_of(root, "testPresets"):
            data["testPresets"].append({"name": name, "inherits": base, "configurePreset": name})
    for kind in ("configurePresets", "buildPresets", "testPresets"):
        if not data[kind]:
            del data[kind]
    if set(data) <= {"version"}:
        path.unlink(missing_ok=True)
    else:
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
        temporary.replace(path)
    return {"preset": name if flags else base, "base": base, "flags": flags}


# --- products: build and run ---------------------------------------------------------------------------------


def product_of(manifest: dict, name: str) -> dict:
    for product in manifest["products"]:
        if product["name"] == name:
            return product
    names = ", ".join(p["name"] for p in manifest["products"]) or "none"
    raise DevError(f"no product {name} in {MANIFEST} (products: {names})")


def expand(parts: list[str], root: Path, preset: str) -> list[str]:
    values = {"root": str(root), "preset": preset, "bin": str(root / "build" / preset / "bin"), "python": sys.executable, "exe": ".exe" if os.name == "nt" else ""}
    return [str(part).format(**values) for part in parts]


def tool_env(root: Path, extra: dict | None = None) -> dict:
    """The environment of every step: the venv of the engine first on PATH (`cmake --preset` runs the reflection
    generator, which needs its jinja2), then the product's own variables."""
    env = {**os.environ, **{k: str(v) for k, v in (extra or {}).items()}}
    bins = [folder / ("Scripts" if os.name == "nt" else "bin") for folder in (root / "engine" / ".venv", root / ".venv")]
    env["PATH"] = os.pathsep.join([str(b) for b in bins if b.is_dir()] + [env.get("PATH", "")])
    return env


def run_step(command: list[str], root: Path, env: dict | None = None) -> None:
    print(f"$ {' '.join(command)}", flush=True)
    result = subprocess.run(command, cwd=root, env=env or tool_env(root))
    if result.returncode != 0:
        raise DevError(f"{command[0]} exited with {result.returncode}")


def build_commands(root: Path, product: dict, preset: str, configure: bool) -> list[list[str]]:
    commands = []
    if configure or not (root / "build" / preset / "CMakeCache.txt").is_file():
        commands.append(["cmake", "--preset", preset])
    targets = product.get("targets") or ([product["target"]] if product.get("target") else [])
    if targets:
        commands.append(["cmake", "--build", "--preset", preset, "--target", *targets])
    commands += [expand(step, root, preset) for step in product.get("after", [])]
    return commands


def pick_preset(root: Path, product: dict, given: str | None) -> str:
    kind = product.get("kind", "desktop")
    if given:
        if kind_of_preset(given) != kind:
            raise DevError(f"{product['name']} is a {kind} product; {given} is a {kind_of_preset(given)} preset")
        return given
    return default_preset(root, kind, product.get("prefer", "debug"))


def cmd_info(root: Path, args) -> None:
    manifest = load_manifest(root)
    info = {
        "root": str(root),
        "manifest": (root / MANIFEST).is_file(),
        "host": platform.system(),
        "presets": preset_groups(root),
        "defaults": {kind: _try(lambda k=kind: default_preset(root, k)) for kind in ("desktop", "web")},
        "devPresets": dev_presets(root),
        "flags": manifest["flags"],
        "products": manifest["products"],
        "chains": manifest["chains"],
        "tools": manifest["tools"],
    }
    if args.json:
        print(json.dumps(info, indent=1))
        return
    print(f"{root} ({'dev.json' if info['manifest'] else 'no dev.json'}), host {info['host']}")
    for group, names in info["presets"].items():
        if names:
            print(f"  {group}: {', '.join(names)}")
    for name, dev in info["devPresets"].items():
        print(f"  {name}: {dev['base']} + {', '.join(f'{k}={v}' for k, v in dev['flags'].items())}")
    for key in ("products", "chains", "tools"):
        if info[key]:
            print(f"{key}: {', '.join(item['name'] for item in info[key])}")


def _try(fn):
    try:
        return fn()
    except DevError:
        return None


def cmd_flags(root: Path, args) -> None:
    sets = {}
    for item in args.set or []:
        key, sep, value = item.partition("=")
        if not sep or not key:
            raise DevError(f"--set {item}: NAME=VALUE")
        sets[key] = value
    if not sets and not args.unset:
        dev = dev_presets(root).get(args.preset if args.preset.endswith(DEV_SUFFIX) else args.preset + DEV_SUFFIX)
        result = {"preset": args.preset, "flags": dev["flags"] if dev else {}}
    else:
        result = set_flags(root, args.preset, sets, args.unset or [])
    if args.json:
        print(json.dumps(result))
    else:
        flags = ", ".join(f"{k}={v}" for k, v in result["flags"].items()) or "no flags"
        print(f"{result['preset']}: {flags}")


def cmd_build(root: Path, args) -> None:
    product = product_of(load_manifest(root), args.product)
    preset = pick_preset(root, product, args.preset)
    for command in build_commands(root, product, preset, args.configure):
        run_step(command, root)


def product_env(root: Path, product: dict) -> dict:
    return tool_env(root, product.get("env"))


def cmd_run(root: Path, args) -> None:
    product = product_of(load_manifest(root), args.product)
    preset = pick_preset(root, product, args.preset)
    if not args.no_build:
        for command in build_commands(root, product, preset, args.configure):
            run_step(command, root)
    command = expand(product.get("run") or [], root, preset) + args.rest
    if not command:
        raise DevError(f"{product['name']} has no run command in {MANIFEST}")
    if product.get("url"):
        print(f"open {product['url']}", flush=True)
    print(f"$ {' '.join(command)}", flush=True)
    os.chdir(root)
    env = product_env(root, product)
    if os.name == "nt":
        sys.exit(subprocess.run(command, env=env).returncode)
    os.execvpe(command[0], command, env)


def cmd_chain(root: Path, args) -> None:
    """Build every product of the chain once, then start its steps together; Ctrl+C (or one step's end) stops all."""
    manifest = load_manifest(root)
    chain = next((c for c in manifest["chains"] if c["name"] == args.name), None)
    if chain is None:
        raise DevError(f"no chain {args.name} in {MANIFEST} (chains: {', '.join(c['name'] for c in manifest['chains']) or 'none'})")
    steps = [step if isinstance(step, dict) else {"product": step} for step in chain["steps"]]
    picked = {"desktop": args.preset, "web": args.web_preset}
    built = set()
    plans = []
    for step in steps:
        product = product_of(manifest, step["product"])
        preset = pick_preset(root, product, picked.get(product.get("kind", "desktop")))
        if not args.no_build and (product["name"], preset) not in built:
            for command in build_commands(root, product, preset, False):
                run_step(command, root)
            built.add((product["name"], preset))
        env = {**product_env(root, product), **{k: str(v) for k, v in (chain.get("env") or {}).items()}, **{k: str(v) for k, v in (step.get("env") or {}).items()}}
        plans.append((product, expand(product.get("run") or [], root, preset) + [str(a) for a in step.get("args", [])], env))
    processes = []
    try:
        for product, command, env in plans:
            print(f"$ {' '.join(command)}", flush=True)
            processes.append(subprocess.Popen(command, cwd=root, env=env))
            time.sleep(float(chain.get("delay", 1)))  # a server listens before its clients come
            if product.get("url"):
                print(f"open {product['url']}", flush=True)
        while all(p.poll() is None for p in processes):
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        for p in processes:
            if p.poll() is None:
                p.send_signal(signal.SIGINT if os.name != "nt" else signal.CTRL_BREAK_EVENT)
        for p in processes:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()


def cmd_tool(root: Path, args) -> None:
    """A tool of dev.json; one with `build` builds that product first and gets its preset as `{preset}` / `{bin}`."""
    manifest = load_manifest(root)
    tool = next((t for t in manifest["tools"] if t["name"] == args.name), None)
    if tool is None:
        raise DevError(f"no tool {args.name} in {MANIFEST} (tools: {', '.join(t['name'] for t in manifest['tools']) or 'none'})")
    preset = args.preset or ""
    if tool.get("build"):
        product = product_of(manifest, tool["build"])
        preset = pick_preset(root, product, args.preset)
        for command in build_commands(root, product, preset, False):
            run_step(command, root)
    command = expand(tool["command"], root, preset) + args.rest
    print(f"$ {' '.join(command)}", flush=True)
    sys.exit(subprocess.run(command, cwd=root, env=tool_env(root)).returncode)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="dev.py", description="Build and run the products of a BagiEngine repository (dev.json).")
    parser.add_argument("--root", help="the repository (default: the nearest folder with CMakePresets.json)")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("info", help="presets, flags, products, chains and tools")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("flags", help="show or change the flags of <preset>-dev in CMakeUserPresets.json")
    p.add_argument("preset")
    p.add_argument("--set", action="append", metavar="NAME=VALUE")
    p.add_argument("--unset", action="append", metavar="NAME")
    p.add_argument("--json", action="store_true")
    for verb in ("build", "run"):
        p = sub.add_parser(verb, help=f"{verb} a product of dev.json")
        p.add_argument("product")
        p.add_argument("--preset", help="default: the host's debug preset (web: web-debug)")
        p.add_argument("--configure", action="store_true", help="run cmake --preset even when the build folder has a cache")
        if verb == "run":
            p.add_argument("--no-build", action="store_true")
    p = sub.add_parser("chain", help="build and start the products of a chain together")
    p.add_argument("name")
    p.add_argument("--preset", help="the desktop preset")
    p.add_argument("--web-preset", help="the web preset")
    p.add_argument("--no-build", action="store_true")
    p = sub.add_parser("tool", help="run a tool of dev.json")
    p.add_argument("name")
    p.add_argument("--preset", help="the preset of the product the tool builds first (its `build`)")
    return parser


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    rest = argv[argv.index("--") + 1 :] if "--" in argv else []
    argv = argv[: argv.index("--")] if "--" in argv else argv
    args = build_parser().parse_args(argv)
    args.rest = rest
    try:
        root = Path(args.root).resolve() if args.root else find_root(Path.cwd())
        {"info": cmd_info, "flags": cmd_flags, "build": cmd_build, "run": cmd_run, "chain": cmd_chain, "tool": cmd_tool}[args.command](root, args)
    except DevError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
