"""A reader of PNG files for the tests of the control API, on the standard library only.

It reads what the engine's `screenshot` writes: 8 bits per channel, RGB or RGBA, no interlacing. Anything else is an
error, because a different file means the engine changed and the test should say so.
"""

import struct
import zlib
from dataclasses import dataclass

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

COLOR_TYPE_RGB = 2
COLOR_TYPE_RGBA = 6
CHANNELS = {COLOR_TYPE_RGB: 3, COLOR_TYPE_RGBA: 4}

FILTER_NONE = 0
FILTER_SUB = 1
FILTER_UP = 2
FILTER_AVERAGE = 3
FILTER_PAETH = 4


class PngError(Exception):
    pass


@dataclass
class PngImage:
    width: int
    height: int
    channels: int
    rows: list[bytes]  # the unfiltered rows, `width * channels` bytes each

    def pixel(self, x: int, y: int) -> tuple[int, ...]:
        start = x * self.channels
        return tuple(self.rows[y][start : start + self.channels])

    def colors(self) -> set[tuple[int, ...]]:
        """Every distinct pixel value of the picture."""
        found: set[tuple[int, ...]] = set()
        for row in self.rows:
            for start in range(0, len(row), self.channels):
                found.add(tuple(row[start : start + self.channels]))
        return found


def _paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distance_left = abs(estimate - left)
    distance_above = abs(estimate - above)
    distance_upper_left = abs(estimate - upper_left)
    if distance_left <= distance_above and distance_left <= distance_upper_left:
        return left
    if distance_above <= distance_upper_left:
        return above
    return upper_left


def _unfilter(kind: int, line: bytearray, previous: bytes, channels: int) -> bytearray:
    for i in range(len(line)):
        left = line[i - channels] if i >= channels else 0
        above = previous[i]
        upper_left = previous[i - channels] if i >= channels else 0
        if kind == FILTER_NONE:
            continue
        if kind == FILTER_SUB:
            line[i] = (line[i] + left) & 0xFF
        elif kind == FILTER_UP:
            line[i] = (line[i] + above) & 0xFF
        elif kind == FILTER_AVERAGE:
            line[i] = (line[i] + (left + above) // 2) & 0xFF
        elif kind == FILTER_PAETH:
            line[i] = (line[i] + _paeth(left, above, upper_left)) & 0xFF
        else:
            raise PngError(f"unknown filter type {kind}")
    return line


def read_png(data: bytes) -> PngImage:
    """Decodes a PNG file; raises PngError for one that is damaged or of a kind that is not supported."""
    if not data.startswith(PNG_SIGNATURE):
        raise PngError("not a PNG file: the signature is wrong")

    position = len(PNG_SIGNATURE)
    header = None
    compressed = bytearray()
    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position : position + 8])
        body = data[position + 8 : position + 8 + length]
        if len(body) != length:
            raise PngError(f"the chunk {kind!r} is cut short")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
        position += 12 + length  # length, type, body, CRC

    if header is None:
        raise PngError("the IHDR chunk is missing")
    width, height, bit_depth, color_type, _compression, _filter_method, interlace = header
    if bit_depth != 8 or color_type not in CHANNELS or interlace != 0:
        raise PngError(f"unsupported PNG: depth {bit_depth}, color type {color_type}, interlace {interlace}")

    channels = CHANNELS[color_type]
    stride = width * channels
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise PngError(f"the pixel data cannot be inflated: {error}") from error
    if len(raw) != (stride + 1) * height:
        raise PngError(f"the pixel data is {len(raw)} bytes, expected {(stride + 1) * height}")

    rows: list[bytes] = []
    previous = bytes(stride)
    for y in range(height):
        start = y * (stride + 1)
        line = _unfilter(raw[start], bytearray(raw[start + 1 : start + 1 + stride]), previous, channels)
        previous = bytes(line)
        rows.append(previous)
    return PngImage(width, height, channels, rows)
