"""Operations on decoded pictures for the screenshot tools (`tools/shots/CLAUDE.md`): crop, drop alpha, scale and write a PNG. Standard library only.

Pure: a `PngImage` in, a `PngImage` or bytes out, with no file and no process. A picture is a list of `bytes` rows, as the reader gives it.
"""

import struct
import sys
import zlib
from pathlib import Path

# The PNG reader lives with the control tools (the game's `ui_shots.py` imports it from there too): one place, so it is not copied.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control" / "tests"))

from png_reader import COLOR_TYPE_RGB, COLOR_TYPE_RGBA, PNG_SIGNATURE, PngImage  # noqa: E402

COLOR_TYPES = {3: COLOR_TYPE_RGB, 4: COLOR_TYPE_RGBA}
BIT_DEPTH = 8
FILTER_NONE = 0
WEIGHT_BITS = 8  # the weights of the bilinear blend are fixed-point numbers of this many bits
WEIGHT_ONE = 1 << WEIGHT_BITS
WEIGHT_HALF = WEIGHT_ONE >> 1  # the rounding term of a blend
HALF = 0.5  # the centre of a pixel


def crop(image: PngImage, x0: int, y0: int, x1: int, y1: int) -> PngImage:
    """The pixels of the box `[x0, y0, x1, y1]`, corners inclusive. A box that is not inside the picture is a ValueError."""
    if not (0 <= x0 <= x1 < image.width and 0 <= y0 <= y1 < image.height):
        raise ValueError(f"the box [{x0},{y0},{x1},{y1}] is not inside the picture {image.width}x{image.height}")
    channels = image.channels
    rows = [row[x0 * channels : (x1 + 1) * channels] for row in image.rows[y0 : y1 + 1]]
    return PngImage(x1 - x0 + 1, y1 - y0 + 1, channels, rows)


def to_channels(image: PngImage, channels: int) -> PngImage:
    """The picture with `channels` channels (3 or 4): a 4 channel picture loses its alpha for 3, a picture that already has them is returned as it is.

    Adding an alpha channel is not offered: there is no honest value for it, so a request for 4 channels of a 3 channel picture is a ValueError.
    """
    if channels not in COLOR_TYPES:
        raise ValueError(f"a picture has 3 or 4 channels, not {channels}")
    if image.channels == channels:
        return image
    if channels > image.channels:
        raise ValueError(f"a picture of {image.channels} channels cannot get {channels}")
    rows = []
    for row in image.rows:
        kept = bytearray(image.width * channels)
        for channel in range(channels):
            kept[channel::channels] = row[channel :: image.channels]
        rows.append(bytes(kept))
    return PngImage(image.width, image.height, channels, rows)


def _sample_points(source: int, target: int) -> list[tuple[int, int, int]]:
    """For each of `target` positions the two neighbours in `source` and the weight (0..255) of the second, the pixel centres aligned."""
    points = []
    for position in range(target):
        at = max(0.0, min(source - 1.0, (position + HALF) * source / target - HALF))
        first = int(at)
        second = min(first + 1, source - 1)
        points.append((first, second, int((at - first) * WEIGHT_ONE)))
    return points


def _blend_row(row: bytes, channels: int, points: list[tuple[int, int, int]]) -> bytes:
    """One row stretched or shrunk along x by the blend of the two neighbours of each point."""
    blended = bytearray(len(points) * channels)
    for channel in range(channels):
        plane = row[channel::channels]
        blended[channel::channels] = bytes(
            (plane[first] * (WEIGHT_ONE - weight) + plane[second] * weight + WEIGHT_HALF) >> WEIGHT_BITS for first, second, weight in points
        )
    return bytes(blended)


def scale_to(image: PngImage, width: int, height: int) -> PngImage:
    """The picture resampled bilinearly to `width` x `height`; the same size gives the same picture."""
    if width < 1 or height < 1:
        raise ValueError(f"a picture cannot be scaled to {width}x{height}")
    if (width, height) == (image.width, image.height):
        return image
    columns = _sample_points(image.width, width)
    lines = _sample_points(image.height, height)
    stretched: dict[int, bytes] = {}  # the source rows already blended along x: a row serves the output rows around it

    def stretched_row(index: int) -> bytes:
        if index not in stretched:
            stretched[index] = _blend_row(image.rows[index], image.channels, columns)
        return stretched[index]

    rows = []
    for first, second, weight in lines:
        upper = stretched_row(first)
        if weight == 0 or first == second:
            rows.append(upper)
            continue
        lower = stretched_row(second)
        rows.append(bytes((a * (WEIGHT_ONE - weight) + b * weight + WEIGHT_HALF) >> WEIGHT_BITS for a, b in zip(upper, lower)))
    return PngImage(width, height, image.channels, rows)


def _chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def encode_png(image: PngImage) -> bytes:
    """The PNG file of an RGB or RGBA picture, 8 bits per channel, rows unfiltered: `read_png(encode_png(x))` is `x`."""
    if image.channels not in COLOR_TYPES:
        raise ValueError(f"a PNG is written from 3 or 4 channels, not {image.channels}")
    header = struct.pack(">IIBBBBB", image.width, image.height, BIT_DEPTH, COLOR_TYPES[image.channels], 0, 0, 0)
    raw = b"".join(bytes([FILTER_NONE]) + row for row in image.rows)
    return PNG_SIGNATURE + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(raw)) + _chunk(b"IEND", b"")
