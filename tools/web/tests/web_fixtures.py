"""Shared by the tests of tools/web: a fake build output made in a temp folder (no binary file is committed)."""

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
