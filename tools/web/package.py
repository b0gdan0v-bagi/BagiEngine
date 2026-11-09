"""Turns the output of an Emscripten build into a folder a static server can serve. Standard library only (brotli optional).

    python tools/web/package.py build/ClangWeb dist
    python tools/web/package.py build/ClangWeb dist --game BagiMatch3

The build folder holds <game>.html, <game>.js, <game>.wasm and, when the game has content, <game>.data (cmake/BagiWeb.cmake). A threaded
build has no more files: emcc 4.0.23 starts its Web Workers from <game>.js itself (no <game>.worker.js). The
output folder gets the page as index.html, the other files under their own names (the page and the script load them by those names),
a gzip copy and, when the `brotli` module can be imported, a brotli copy of every .html, .js, .wasm and .data next to the original
(name.gz, name.br; serve.py picks them by Accept-Encoding) and manifest.json with the size and the SHA-256 of every file. A file of
an earlier run that this run does not make (another game's, a .data the game no longer has) is deleted from the output folder, so the
folder holds what the manifest lists and nothing else of the kinds named here; other files in it are left alone.
"""

import argparse
import gzip
import hashlib
import importlib
import json
import shutil
import sys
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


def package(build_dir: Path, out_dir: Path, game: str | None = None, brotli_module="auto") -> dict:
    """Copies the build output into `out_dir` and returns the manifest (also written as manifest.json).

    `brotli_module` is the module used for the .br copies: "auto" imports `brotli` when it is installed, None leaves them out."""
    if not build_dir.is_dir():
        raise PackageError(f"{build_dir} is not a folder")
    if brotli_module == "auto":
        brotli_module = load_brotli()
    page = find_page(build_dir, game)
    stem = page.stem

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

    manifest = {"game": stem, "files": files}
    (out_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package the output of a web build into a folder for serve.py.")
    parser.add_argument("build_dir", type=Path, help="folder with <game>.html, .js, .wasm and .data (build/ClangWeb)")
    parser.add_argument("out_dir", type=Path, help="folder to write (dist)")
    parser.add_argument("--game", help="name of the game (BagiMatch3); needed only when the build folder holds several pages")
    args = parser.parse_args(argv)
    try:
        manifest = package(args.build_dir, args.out_dir, args.game)
    except PackageError as error:
        print(f"package.py: {error}", file=sys.stderr)
        return 1
    total = sum(entry["size"] for entry in manifest["files"].values())
    print(f"package.py: {manifest['game']}: {len(manifest['files'])} files, {total} bytes, written to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
