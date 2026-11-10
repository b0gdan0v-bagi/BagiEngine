"""Turns the output of an Emscripten build into a folder a static server can serve. Standard library only (brotli optional).

    python tools/web/package.py build/web-release/bin dist
    python tools/web/package.py build/web-release/bin dist --game BagiMatch3
    python tools/web/package.py build/web-release-single-thread/bin dist-single --single-thread
    python tools/web/package.py build/web-release/bin dist --app web/app.json

The build folder holds <game>.html, <game>.js, <game>.wasm and, when the game has content, <game>.data (cmake/BagiWeb.cmake). A threaded
build has no more files: emcc 4.0.23 starts its Web Workers from <game>.js itself (no <game>.worker.js). The
output folder gets the page as index.html, the other files under their own names (the page and the script load them by those names),
a gzip copy and, when the `brotli` module can be imported, a brotli copy of every .html, .js, .wasm and .data next to the original
(name.gz, name.br; serve.py picks them by Accept-Encoding) and manifest.json with `threads` (true for a threaded build, the default; false
with --single-thread: serve.py sends the cross-origin isolation headers only to a threaded page) and the size and the SHA-256 of every
file. A file of
an earlier run that this run does not make (another game's, a .data the game no longer has) is deleted from the output folder, so the
folder holds what the manifest lists and nothing else of the kinds named here; other files in it are left alone.

The web app manifest is written too: manifest.webmanifest (never manifest.json, which is the package manifest above), with `display:
fullscreen`, and the folder icons/ with the PNGs it lists plus icons/apple-touch-icon.png (the largest of them; iOS scales it). They come
from the game's file `--app web/app.json`, all keys optional: `name`, `short_name`, `orientation` (any | landscape | portrait),
`background_color`, `theme_color` (#rrggbb) and `icons` (a list of {"src": a PNG path relative to the file, "sizes", "purpose"}). Without
the file, or without icons in it, the defaults are the game's name, the color #101114 and two generated flat-color PNGs (192 and 512): a
page with no icons is not installable on Chrome. icons/ is deleted and written again on each run. These files are listed in manifest.json
(size and SHA-256) and have no .gz / .br copy.
"""

import argparse
import gzip
import hashlib
import importlib
import json
import re
import shutil
import struct
import sys
import zlib
from pathlib import Path

INDEX_NAME = "index.html"
MANIFEST_NAME = "manifest.json"
PAGE_SUFFIX = ".html"
# The files of a build next to the page: the script and the module are always there, the package only when the game has content
REQUIRED_SUFFIXES = (".js", ".wasm")
OPTIONAL_SUFFIXES = (".data",)
# What is worth compressing: the text of the page and the script, and the two large binaries (the wasm and the content package)
COMPRESSED_SUFFIXES = (".html", ".js", ".wasm", ".data")
GZIP_LEVEL = 9
BROTLI_QUALITY = 11
HASH_CHUNK = 1 << 20

# The web app manifest and its icons (the page links them; the name is never manifest.json, that is the package manifest)
WEB_MANIFEST_NAME = "manifest.webmanifest"
ICONS_FOLDER = "icons"
APPLE_TOUCH_ICON_NAME = "apple-touch-icon.png"
ORIENTATIONS = ("any", "landscape", "portrait")
DEFAULT_ORIENTATION = "any"
DEFAULT_COLOR = "#101114"
COLOR_PATTERN = re.compile(r"#[0-9a-fA-F]{6}")
# The sizes Chrome asks of an installable page
GENERATED_ICON_SIZES = (192, 512)
GENERATED_ICON_PURPOSE = "any maskable"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_IHDR_END = 24
PNG_COMPRESSION_LEVEL = 9


class PackageError(Exception):
    """The build output is not what package() needs; the message says what is missing."""


def load_brotli():
    """The `brotli` module, or None when it is not installed (the .br copies are then left out)."""
    try:
        return importlib.import_module("brotli")
    except ImportError:
        return None


def find_page(build_dir: Path, game: str | None) -> Path:
    """The <game>.html of the build; without `game` the folder must hold exactly one page."""
    if game:
        page = build_dir / f"{game}{PAGE_SUFFIX}"
        if not page.is_file():
            raise PackageError(f"{page} not found: build the game for the web first")
        return page
    pages = sorted(build_dir.glob(f"*{PAGE_SUFFIX}"))
    if not pages:
        raise PackageError(f"no *{PAGE_SUFFIX} in {build_dir}: build the game for the web first")
    if len(pages) > 1:
        names = ", ".join(page.name for page in pages)
        raise PackageError(f"{build_dir} holds several pages ({names}): name the game with --game")
    return pages[0]


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(HASH_CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def write_compressed_copies(path: Path, brotli_module) -> dict:
    """Writes name.gz (and name.br) next to `path`; returns {"gzip": size, "brotli": size} of the copies made.

    A copy left by an earlier package run is removed first, so a run without brotli never leaves a stale .br beside a new file."""
    data = path.read_bytes()
    gzip_path = path.with_name(path.name + ".gz")
    brotli_path = path.with_name(path.name + ".br")
    brotli_path.unlink(missing_ok=True)
    sizes = {}
    # mtime=0 keeps the bytes of the copy the same for the same input
    gzip_path.write_bytes(gzip.compress(data, compresslevel=GZIP_LEVEL, mtime=0))
    sizes["gzip"] = gzip_path.stat().st_size
    if brotli_module is not None:
        brotli_path.write_bytes(brotli_module.compress(data, quality=BROTLI_QUALITY))
        sizes["brotli"] = brotli_path.stat().st_size
    return sizes


def remove_stale_files(out_dir: Path, kept: set) -> None:
    """Deletes the files of `out_dir` that an earlier run could have made and this one does not: a page, script, module or package
    (and their .gz / .br copies) whose name is not in `kept`. Anything else in the folder is not ours and stays."""
    owned = {PAGE_SUFFIX, *REQUIRED_SUFFIXES, *OPTIONAL_SUFFIXES}
    for path in out_dir.iterdir():
        original = path.name.removesuffix(".gz").removesuffix(".br")
        if path.is_file() and original not in kept and Path(original).suffix in owned:
            path.unlink()


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def flat_color_png(size: int, color: str) -> bytes:
    """A square RGB PNG of one color (`#rrggbb`). Pure and deterministic: the same arguments give the same bytes."""
    red, green, blue = (int(color[index:index + 2], 16) for index in (1, 3, 5))
    row = b"\x00" + bytes((red, green, blue)) * size
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (PNG_SIGNATURE + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(row * size, PNG_COMPRESSION_LEVEL))
            + png_chunk(b"IEND", b""))


def png_size(data: bytes) -> tuple[int, int] | None:
    """(width, height) from the IHDR of a PNG, None when `data` does not start like one."""
    if len(data) < PNG_IHDR_END or not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", data[16:PNG_IHDR_END])
    return (width, height) if width > 0 and height > 0 else None


def read_text_member(app: dict, key: str, app_file: Path) -> str | None:
    value = app.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or not value.strip():
        raise PackageError(f"{app_file}: `{key}` must be a non-empty string")
    return value


def read_color(app: dict, key: str, app_file: Path) -> str | None:
    value = app.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or not COLOR_PATTERN.fullmatch(value):
        raise PackageError(f"{app_file}: `{key}` must be a color like #101114, not {value!r}")
    return value


def read_icon(entry, app_file: Path) -> dict:
    """One icon of app.json checked against the disk: {"source": Path, "name": basename, "sizes": "WxH", "purpose": str | None, "area": int}."""
    if not isinstance(entry, dict) or not isinstance(entry.get("src"), str) or not entry["src"]:
        raise PackageError(f"{app_file}: every icon needs a `src` string")
    folder = app_file.parent.resolve()
    source = (folder / entry["src"]).resolve()
    if source != folder and folder not in source.parents:
        raise PackageError(f"{app_file}: icon {entry['src']} is outside the folder of the file")
    if not source.is_file():
        raise PackageError(f"{app_file}: icon {entry['src']} not found")
    size = png_size(source.read_bytes())
    if size is None:
        raise PackageError(f"{app_file}: icon {entry['src']} is not a PNG")
    sizes = entry.get("sizes", f"{size[0]}x{size[1]}")
    purpose = entry.get("purpose")
    if not isinstance(sizes, str) or not sizes or (purpose is not None and not isinstance(purpose, str)):
        raise PackageError(f"{app_file}: icon {entry['src']}: `sizes` and `purpose` must be strings")
    return {"source": source, "name": source.name, "sizes": sizes, "purpose": purpose, "area": size[0] * size[1]}


def read_app_file(app_file: Path | None, default_name: str) -> dict:
    """The settings of the web app manifest: the keys of `app_file` (None: no file) over the defaults. Every error is a PackageError.

    The result has name, short_name, orientation, background_color, theme_color and icons (the list of read_icon(); empty when the file lists none)."""
    app = {}
    if app_file is not None:
        try:
            app = json.loads(app_file.read_text(encoding="utf-8"))
        except OSError as error:
            raise PackageError(f"cannot read {app_file}: {error}") from error
        except ValueError as error:
            raise PackageError(f"{app_file} is not valid JSON: {error}") from error
        if not isinstance(app, dict):
            raise PackageError(f"{app_file} must hold a JSON object")
    shown = app_file or Path("app.json")
    name = read_text_member(app, "name", shown) or default_name
    orientation = app.get("orientation", DEFAULT_ORIENTATION)
    if orientation not in ORIENTATIONS:
        raise PackageError(f"{shown}: `orientation` must be one of {', '.join(ORIENTATIONS)}, not {orientation!r}")
    icons = app.get("icons", [])
    if not isinstance(icons, list):
        raise PackageError(f"{shown}: `icons` must be a list")
    icons = [read_icon(entry, shown) for entry in icons]
    names = [icon["name"] for icon in icons]
    for icon_name in names:
        if names.count(icon_name) > 1 or icon_name == APPLE_TOUCH_ICON_NAME:
            raise PackageError(f"{shown}: the icon file name {icon_name} is used twice or is reserved: icons are copied by their file name")
    return {
        "name": name,
        "short_name": read_text_member(app, "short_name", shown) or name,
        "orientation": orientation,
        "background_color": read_color(app, "background_color", shown) or DEFAULT_COLOR,
        "theme_color": read_color(app, "theme_color", shown) or DEFAULT_COLOR,
        "icons": icons,
    }


def write_web_app(out_dir: Path, settings: dict) -> list[str]:
    """Writes manifest.webmanifest and icons/ (cleaned first) into `out_dir`; returns the names (relative, with /) of the files written."""
    icons_dir = out_dir / ICONS_FOLDER
    if icons_dir.exists():
        shutil.rmtree(icons_dir)
    icons_dir.mkdir()
    icons = settings["icons"]
    written = []
    listed = []
    if icons:
        for icon in icons:
            shutil.copyfile(icon["source"], icons_dir / icon["name"])
            written.append(f"{ICONS_FOLDER}/{icon['name']}")
            listed.append({"src": f"{ICONS_FOLDER}/{icon['name']}", "sizes": icon["sizes"], "type": "image/png",
                           **({"purpose": icon["purpose"]} if icon["purpose"] else {})})
        largest = max(icons, key=lambda icon: icon["area"])
        shutil.copyfile(largest["source"], icons_dir / APPLE_TOUCH_ICON_NAME)
    else:
        for size in GENERATED_ICON_SIZES:
            name = f"icon-{size}.png"
            (icons_dir / name).write_bytes(flat_color_png(size, settings["theme_color"]))
            written.append(f"{ICONS_FOLDER}/{name}")
            listed.append({"src": f"{ICONS_FOLDER}/{name}", "sizes": f"{size}x{size}", "type": "image/png", "purpose": GENERATED_ICON_PURPOSE})
        (icons_dir / APPLE_TOUCH_ICON_NAME).write_bytes(flat_color_png(max(GENERATED_ICON_SIZES), settings["theme_color"]))
    written.append(f"{ICONS_FOLDER}/{APPLE_TOUCH_ICON_NAME}")
    web_manifest = {
        "name": settings["name"],
        "short_name": settings["short_name"],
        "id": "./",
        "start_url": "./",
        "scope": "./",
        "display": "fullscreen",
        "display_override": ["fullscreen", "standalone"],
        "orientation": settings["orientation"],
        "background_color": settings["background_color"],
        "theme_color": settings["theme_color"],
        "icons": listed,
    }
    (out_dir / WEB_MANIFEST_NAME).write_text(json.dumps(web_manifest, indent=2) + "\n", encoding="utf-8")
    return [WEB_MANIFEST_NAME, *written]


def package(build_dir: Path, out_dir: Path, game: str | None = None, brotli_module="auto", threads: bool = True,
            app_file: Path | None = None) -> dict:
    """Copies the build output into `out_dir` and returns the manifest (also written as manifest.json).

    `brotli_module` is the module used for the .br copies: "auto" imports `brotli` when it is installed, None leaves them out.
    `threads` says whether the build is threaded (the manifest's `threads`, which serve.py reads to decide on the isolation headers).
    `app_file` is the game's web/app.json for the web app manifest and the icons (None: the defaults, generated icons)."""
    if not build_dir.is_dir():
        raise PackageError(f"{build_dir} is not a folder")
    if brotli_module == "auto":
        brotli_module = load_brotli()
    page = find_page(build_dir, game)
    stem = page.stem
    # Read before anything is written, so a bad app file leaves the output folder as it was
    settings = read_app_file(app_file, stem)

    # (source, name in the output folder)
    sources = [(page, INDEX_NAME)]
    for suffix in REQUIRED_SUFFIXES:
        source = build_dir / f"{stem}{suffix}"
        if not source.is_file():
            raise PackageError(f"{source} not found: the build output is incomplete")
        sources.append((source, source.name))
    for suffix in OPTIONAL_SUFFIXES:
        source = build_dir / f"{stem}{suffix}"
        if source.is_file():
            sources.append((source, source.name))
        elif source.name.encode() in (build_dir / f"{stem}.js").read_bytes():
            # emcc writes the name of the package into the script that loads it: a script that names a package which is not
            # there is a build that lost it, and the page would stop at the first request
            raise PackageError(f"{source} not found, but {stem}.js loads it: the build output is incomplete")

    # The output folder is cleaned of the files of earlier runs, so it must not be the build folder (it would delete the page) or hold it
    build_resolved, out_resolved = build_dir.resolve(), out_dir.resolve()
    if out_resolved == build_resolved or out_resolved in build_resolved.parents or build_resolved in out_resolved.parents:
        raise PackageError(f"{out_dir} and the build folder {build_dir} are the same folder or one is inside the other: write the package to a folder of its own")
    out_dir.mkdir(parents=True, exist_ok=True)
    remove_stale_files(out_dir, {name for _, name in sources})
    files = {}
    for source, name in sources:
        target = out_dir / name
        shutil.copyfile(source, target)
        entry = {"size": target.stat().st_size, "sha256": sha256_of(target)}
        if target.suffix in COMPRESSED_SUFFIXES:
            entry.update(write_compressed_copies(target, brotli_module))
        files[name] = entry
    for name in write_web_app(out_dir, settings):
        target = out_dir / name
        files[name] = {"size": target.stat().st_size, "sha256": sha256_of(target)}

    manifest = {"game": stem, "threads": threads, "files": files}
    (out_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package the output of a web build into a folder for serve.py.")
    parser.add_argument("build_dir", type=Path, help="folder with <game>.html, .js, .wasm and .data (build/web-release/bin)")
    parser.add_argument("out_dir", type=Path, help="folder to write (dist)")
    parser.add_argument("--game", help="name of the game (BagiMatch3); needed only when the build folder holds several pages")
    parser.add_argument("--single-thread", action="store_true",
                        help="the build is the single-thread page (web-*-single-thread): the manifest says threads: false and serve.py sends no COOP / COEP for it")
    parser.add_argument("--app", type=Path, metavar="FILE",
                        help="the game's web/app.json: name, short_name, orientation, colors and icons of the web app manifest (optional)")
    args = parser.parse_args(argv)
    try:
        manifest = package(args.build_dir, args.out_dir, args.game, threads=not args.single_thread, app_file=args.app)
    except PackageError as error:
        print(f"package.py: {error}", file=sys.stderr)
        return 1
    total = sum(entry["size"] for entry in manifest["files"].values())
    kind = "threaded" if manifest["threads"] else "single-thread"
    print(f"package.py: {manifest['game']} ({kind}): {len(manifest['files'])} files, {total} bytes, written to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
