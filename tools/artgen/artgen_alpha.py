"""
The alpha channel of a PNG without PIL (tools/artgen/CLAUDE.md, "The service"): `read_alpha` gives one byte of alpha per pixel,
for the checks of a cut-out in `artgen_checks`.

Every PNG filter works per channel, with the pixel size as the offset, so the alpha bytes of a row depend only on the alpha bytes
of the rows above: the other channels are never unfiltered. Standard library only.
"""

import itertools
import struct
import zlib
from dataclasses import dataclass

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHUNK_HEAD = 8  # length (4) and type (4)
CHUNK_CRC = 4
HEADER_SIZE = 13
PALETTE_ENTRY = 3
MAX_PIXELS = 36 * 1024 * 1024  # a 1024 picture is 1 M; this bounds what a broken header can make us allocate
OPAQUE = 255

COLOUR_GREY = 0
COLOUR_RGB = 2
COLOUR_PALETTE = 3
COLOUR_GREY_ALPHA = 4
COLOUR_RGBA = 6
# Colour type -> the bit depths the format allows and the number of channels.
COLOUR_TYPES = {
    COLOUR_GREY: ((1, 2, 4, 8, 16), 1),
    COLOUR_RGB: ((8, 16), 3),
    COLOUR_PALETTE: ((1, 2, 4, 8), 1),
    COLOUR_GREY_ALPHA: ((8, 16), 2),
    COLOUR_RGBA: ((8, 16), 4),
}
INTERLACE_NONE = 0

FILTER_NONE = 0
FILTER_SUB = 1
FILTER_UP = 2
FILTER_AVERAGE = 3
FILTER_PAETH = 4


class AlphaError(Exception):
    """A PNG whose alpha cannot be read; the message says why."""


@dataclass(frozen=True)
class Alpha:
    width: int
    height: int
    pixels: bytes  # width * height bytes, row by row


def _chunks(data):
    """The (type, body) of every chunk up to IEND, each checked against its CRC."""
    if data[:len(PNG_SIGNATURE)] != PNG_SIGNATURE:
        raise AlphaError("not a PNG picture")
    position = len(PNG_SIGNATURE)
    while True:
        if position + CHUNK_HEAD > len(data):
            raise AlphaError("the PNG is truncated")
        length, kind = struct.unpack_from("!I4s", data, position)
        body_start = position + CHUNK_HEAD
        body_end = body_start + length
        if body_end + CHUNK_CRC > len(data):
            raise AlphaError("the PNG is truncated")
        body = data[body_start:body_end]
        (crc,) = struct.unpack_from("!I", data, body_end)
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise AlphaError(f"the PNG chunk {kind.decode('latin-1')} has a bad CRC")
        yield kind, body
        if kind == b"IEND":
            return
        position = body_end + CHUNK_CRC


def _parse_header(body):
    if len(body) != HEADER_SIZE:
        raise AlphaError("the PNG header has the wrong size")
    width, height, depth, colour, compression, filter_method, interlace = struct.unpack("!IIBBBBB", body)
    if width == 0 or height == 0 or width * height > MAX_PIXELS:
        raise AlphaError(f"the PNG size {width}x{height} is not supported")
    if colour not in COLOUR_TYPES or depth not in COLOUR_TYPES[colour][0]:
        raise AlphaError(f"the PNG colour type {colour} with bit depth {depth} is not valid")
    if compression != 0 or filter_method != 0:
        raise AlphaError("the PNG uses an unknown compression or filter method")
    if interlace != INTERLACE_NONE:
        raise AlphaError("an interlaced PNG is not supported")
    return width, height, depth, colour


def _collect(data):
    header = None
    palette_size = None
    transparency = None
    idat = []
    for kind, body in _chunks(data):
        if header is None:
            if kind != b"IHDR":
                raise AlphaError("the PNG does not start with its header")
            header = _parse_header(body)
        elif kind == b"IHDR":
            raise AlphaError("the PNG has two headers")
        elif kind == b"PLTE":
            palette_size = len(body) // PALETTE_ENTRY
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            idat.append(body)
    return header, palette_size, transparency, b"".join(idat)


def _inflate(stream, expected):
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(stream, expected + 1)
    except zlib.error as error:
        raise AlphaError(f"the PNG image data is broken: {error}") from error
    if len(raw) > expected:
        raise AlphaError("the PNG image data is longer than its header says")
    if len(raw) < expected or not decoder.eof:
        raise AlphaError("the PNG image data is truncated")
    return raw


def _paeth(left, up, up_left):
    estimate = left + up - up_left
    distance_left = abs(estimate - left)
    distance_up = abs(estimate - up)
    distance_up_left = abs(estimate - up_left)
    if distance_left <= distance_up and distance_left <= distance_up_left:
        return left
    return up if distance_up <= distance_up_left else up_left


def _unfilter(kind, row, previous):
    """One row of a single channel (offset 1, so `Sub` looks at the pixel to the left)."""
    if kind == FILTER_NONE:
        return row
    if kind == FILTER_SUB:
        return bytes(value & 0xFF for value in itertools.accumulate(row))
    if kind == FILTER_UP:
        return bytes([(value + above) & 0xFF for value, above in zip(row, previous)])
    result = bytearray(len(row))
    left = 0
    up_left = 0
    if kind == FILTER_AVERAGE:
        for index, value in enumerate(row):
            left = (value + ((left + previous[index]) >> 1)) & 0xFF
            result[index] = left
        return bytes(result)
    if kind == FILTER_PAETH:
        for index, value in enumerate(row):
            above = previous[index]
            left = (value + _paeth(left, above, up_left)) & 0xFF
            result[index] = left
            up_left = above
        return bytes(result)
    raise AlphaError(f"the PNG has an unknown filter type {kind}")


def _channel(raw, width, height, stride, pixel_bytes, offset):
    """The unfiltered bytes of one channel: byte `offset` of every pixel, row by row."""
    rows = []
    previous = bytes(width)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        row = raw[start + 1 + offset:start + 1 + stride:pixel_bytes]
        previous = _unfilter(kind, row, previous)
        rows.append(previous)
    return b"".join(rows)


def read_alpha(data):
    """The alpha of a PNG, or None for a picture that has none (grey or RGB without `tRNS`: opaque). Reads colour types 6, 4
    and 3 (a palette with `tRNS`) at bit depth 8, and 6 and 4 at 16 (the high byte), not interlaced. Raises AlphaError for
    anything else and for a damaged file."""
    header, palette_size, transparency, stream = _collect(data)
    if header is None:
        raise AlphaError("the PNG has no header")
    width, height, depth, colour = header
    if colour in (COLOUR_GREY, COLOUR_RGB):
        if transparency is not None:
            raise AlphaError("a PNG whose transparency is a colour key is not supported")
        return None
    if colour == COLOUR_PALETTE:
        if palette_size is None:
            raise AlphaError("the PNG has no palette")
        if transparency is None:
            return None
        if len(transparency) > palette_size:
            raise AlphaError("the PNG has more transparency entries than palette entries")
    if depth < 8:
        raise AlphaError(f"the PNG bit depth {depth} is not supported")
    channels = COLOUR_TYPES[colour][1]
    sample_bytes = depth // 8
    pixel_bytes = channels * sample_bytes
    stride = width * pixel_bytes
    raw = _inflate(stream, height * (stride + 1))
    # The alpha is the last channel; of two bytes the high one is read. A palette's only channel is the index.
    offset = 0 if colour == COLOUR_PALETTE else (channels - 1) * sample_bytes
    pixels = _channel(raw, width, height, stride, pixel_bytes, offset)
    if colour == COLOUR_PALETTE:
        table = bytes(transparency) + bytes([OPAQUE]) * (256 - len(transparency))
        pixels = pixels.translate(table)
    return Alpha(width, height, pixels)
