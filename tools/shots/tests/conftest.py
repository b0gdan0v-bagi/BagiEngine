import struct
import sys
import zlib
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))
sys.path.insert(0, str(TOOL_DIR.parent / "control" / "tests"))

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
COLOR_TYPE_RGB = 2
COLOR_TYPE_RGBA = 6


def _chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def write_png(width: int, height: int, pixel_fn, color_type: int = COLOR_TYPE_RGBA) -> bytes:
    """A PNG whose pixel (x, y) is `pixel_fn(x, y)`, a tuple of bytes for its channels (RGBA by default; unfiltered rows)."""
    raw = b"".join(b"\x00" + b"".join(bytes(pixel_fn(x, y)) for x in range(width)) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return PNG_SIGNATURE + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(raw)) + _chunk(b"IEND", b"")
