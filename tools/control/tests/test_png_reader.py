import struct
import zlib

import pytest

from png_reader import (
    FILTER_AVERAGE,
    FILTER_NONE,
    FILTER_PAETH,
    FILTER_SUB,
    FILTER_UP,
    PNG_SIGNATURE,
    PngError,
    read_png,
)


def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def make_png(width: int, height: int, filtered_rows: list[tuple[int, bytes]], color_type: int = 6, bit_depth: int = 8, interlace: int = 0) -> bytes:
    raw = b"".join(bytes([kind]) + body for kind, body in filtered_rows)
    header = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace)
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def test_reads_an_unfiltered_rgba_picture():
    png = make_png(2, 2, [(FILTER_NONE, bytes([1, 2, 3, 255, 4, 5, 6, 255])), (FILTER_NONE, bytes([7, 8, 9, 255, 10, 11, 12, 255]))])

    image = read_png(png)

    assert (image.width, image.height, image.channels) == (2, 2, 4)
    assert image.pixel(0, 0) == (1, 2, 3, 255)
    assert image.pixel(1, 0) == (4, 5, 6, 255)
    assert image.pixel(1, 1) == (10, 11, 12, 255)


def test_reads_an_rgb_picture():
    image = read_png(make_png(2, 1, [(FILTER_NONE, bytes([9, 8, 7, 6, 5, 4]))], color_type=2))

    assert image.channels == 3
    assert image.pixel(1, 0) == (6, 5, 4)


def test_undoes_every_filter():
    # Two rows of two RGB pixels; the first row is the plain one the others are built against
    first = bytes([10, 20, 30, 40, 50, 60])
    sub = bytes([10, 20, 30, 30, 30, 30])  # each byte minus the one a pixel to the left
    up = bytes([1, 1, 1, 1, 1, 1])  # minus the row above
    average = bytes([6, 11, 16, 20, 25, 30])
    paeth = bytes([1, 1, 1, 1, 1, 1])

    image = read_png(make_png(2, 2, [(FILTER_NONE, first), (FILTER_UP, up)], color_type=2))
    assert image.rows[1] == bytes([11, 21, 31, 41, 51, 61])

    image = read_png(make_png(2, 1, [(FILTER_SUB, sub)], color_type=2))
    assert image.rows[0] == bytes([10, 20, 30, 40, 50, 60])

    image = read_png(make_png(2, 2, [(FILTER_NONE, first), (FILTER_AVERAGE, average)], color_type=2))
    # first pixel: x + (0 + above) // 2, second: x + (left + above) // 2
    assert image.rows[1][:3] == bytes([(6 + 5) & 0xFF, (11 + 10) & 0xFF, (16 + 15) & 0xFF])

    image = read_png(make_png(2, 2, [(FILTER_NONE, first), (FILTER_PAETH, paeth)], color_type=2))
    # first pixel has no left and no upper left, so Paeth picks the one above
    assert image.rows[1][:3] == bytes([11, 21, 31])


def test_lists_the_distinct_colors():
    png = make_png(3, 1, [(FILTER_NONE, bytes([1, 1, 1, 255, 2, 2, 2, 255, 1, 1, 1, 255]))])

    assert read_png(png).colors() == {(1, 1, 1, 255), (2, 2, 2, 255)}


@pytest.mark.parametrize(
    "data",
    [
        b"",
        b"not a png at all",
        PNG_SIGNATURE,
        PNG_SIGNATURE + chunk(b"IEND", b""),
        make_png(2, 2, [(FILTER_NONE, bytes(8))]),  # one row short
    ],
)
def test_refuses_damaged_files(data):
    with pytest.raises(PngError):
        read_png(data)


def test_refuses_what_the_engine_does_not_write():
    with pytest.raises(PngError):
        read_png(make_png(1, 1, [(FILTER_NONE, bytes(4))], bit_depth=16))
    with pytest.raises(PngError):
        read_png(make_png(1, 1, [(FILTER_NONE, bytes(1))], color_type=0))
    with pytest.raises(PngError):
        read_png(make_png(1, 1, [(FILTER_NONE, bytes(4))], interlace=1))


def test_refuses_an_unknown_filter():
    with pytest.raises(PngError):
        read_png(make_png(1, 1, [(9, bytes(4))]))
