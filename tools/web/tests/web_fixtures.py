"""Shared by the tests of tools/web: a fake build output made in a temp folder (no binary file is committed)."""

import json
import struct
import sys
from pathlib import Path

# tools/web, where package.py, serve.py and build_and_deploy.py are (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

GAME = "BagiFake"
# Repeated text compresses, so the compressed copies are smaller than the originals
CONTENT = {
    f"{GAME}.html": b'<!doctype html><html><body><canvas id="canvas"></canvas><script src="BagiFake.js"></script></body></html>\n',
    f"{GAME}.js": b"var Module = {};\n" * 50,
    f"{GAME}.wasm": b"\x00asm\x01\x00\x00\x00" + bytes(range(256)) * 20,
    f"{GAME}.data": b"config bytes " * 100,
}


class FakeBrotli:
    """Stands in for the `brotli` module, which is not installed everywhere: the 'compressed' data is the input behind a marker."""

    MARKER = b"BR:"

    @staticmethod
    def compress(data: bytes, quality: int = 11) -> bytes:
        return FakeBrotli.MARKER + data


def write_build_output(folder: Path, names=tuple(CONTENT)) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    for name in names:
        (folder / name).write_bytes(CONTENT[name])
    return folder


def png_header_size(data: bytes) -> tuple[int, int]:
    """(width, height) read from the IHDR of a PNG by the test itself (not by package.py), after checking the signature."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR", "not a PNG"
    return struct.unpack(">II", data[16:24])


def write_app_file(folder: Path, app: dict, icons: dict | None = None) -> Path:
    """`folder`/app.json with the keys of `app`, and a square PNG of the given side for each path of `icons` ({relative path: side})."""
    import package  # tools/web is on sys.path since the top of this file
    folder.mkdir(parents=True, exist_ok=True)
    for relative, side in (icons or {}).items():
        target = folder / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(package.flat_color_png(side, "#204060"))
    path = folder / "app.json"
    path.write_text(json.dumps(app), encoding="utf-8")
    return path
