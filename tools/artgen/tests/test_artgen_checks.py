"""The checks of a cut-out (tools/artgen/CLAUDE.md, "The service"): the PNG alpha reader and, below it, the checks over the alpha."""

import struct
import sys
import zlib
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

from artgen_alpha import Alpha, AlphaError, read_alpha  # noqa: E402

SIGNATURE = b"\x89PNG\r\n\x1a\n"
GREY, RGB, PALETTE, GREY_ALPHA, RGBA = 0, 2, 3, 4, 6
CHANNELS = {GREY: 1, RGB: 3, PALETTE: 1, GREY_ALPHA: 2, RGBA: 4}
FILTERS = (0, 1, 2, 3, 4)


def chunk(kind, body):
    return struct.pack("!I", len(body)) + kind + body + struct.pack("!I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(kind, row, previous, step):
    """The forward filter of the PNG specification, over every byte with the pixel size `step` as the offset."""
    out = bytearray()
    for i, value in enumerate(row):
        a = row[i - step] if i >= step else 0
        b = previous[i]
        c = previous[i - step] if i >= step else 0
        predictor = (0, a, b, (a + b) // 2, paeth(a, b, c))[kind]
        out.append((value - predictor) & 0xFF)
    return bytes([kind]) + bytes(out)


def encode(width, height, colour, depth, rows, filters=(0,), extra=(), interlace=0, split_idat=False):
    """A PNG of `rows` (the pixel bytes of each row, `depth` 8 or 16), the filter of row y being filters[y % len(filters)]."""
    step = CHANNELS[colour] * depth // 8
    previous = bytes(width * step)
    raw = b""
    for y, row in enumerate(rows):
        raw += filter_row(filters[y % len(filters)], row, previous, step)
        previous = row
    compressed = zlib.compress(raw)
    header = struct.pack("!IIBBBBB", width, height, depth, colour, 0, 0, interlace)
    body = SIGNATURE + chunk(b"IHDR", header)
    for kind, data in extra:
        body += chunk(kind, data)
    if split_idat:
        half = len(compressed) // 2
        body += chunk(b"IDAT", compressed[:half]) + chunk(b"IDAT", compressed[half:])
    else:
        body += chunk(b"IDAT", compressed)
    return body + chunk(b"IEND", b"")


def pattern(width, height, channels, sample=1):
    """Pixel bytes that differ per pixel, channel and row, so a wrong filter shows."""
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            for c in range(channels):
                value = (x * 37 + y * 91 + c * 53 + (x * y) % 7 * 29) & 0xFF
                row += bytes([value]) + bytes([(value * 3 + 11) & 0xFF]) * (sample - 1)
        rows.append(bytes(row))
    return rows


def expected_alpha(rows, width, channels, sample=1):
    step = channels * sample
    return b"".join(row[(channels - 1) * sample::step] for row in rows)


@pytest.mark.parametrize("kind", FILTERS)
@pytest.mark.parametrize("colour", [RGBA, GREY_ALPHA])
@pytest.mark.parametrize("depth", [8, 16])
def test_alpha_every_filter_of_every_alpha_colour_type(kind, colour, depth):
    width, height = 9, 6
    sample = depth // 8
    rows = pattern(width, height, CHANNELS[colour], sample)
    alpha = read_alpha(encode(width, height, colour, depth, rows, filters=(kind,)))
    assert alpha == Alpha(width, height, expected_alpha(rows, width, CHANNELS[colour], sample))


def test_alpha_mixed_filters_and_split_idat():
    width, height = 13, 11
    rows = pattern(width, height, 4)
    alpha = read_alpha(encode(width, height, RGBA, 8, rows, filters=FILTERS, split_idat=True))
    assert alpha.pixels == expected_alpha(rows, width, 4)


def test_alpha_16_bit_takes_the_high_byte():
    rows = [bytes([10, 0, 20, 0, 30, 0, 0xAB, 0xCD, 10, 0, 20, 0, 30, 0, 0x12, 0x34])]
    assert read_alpha(encode(2, 1, RGBA, 16, rows)).pixels == bytes([0xAB, 0x12])


@pytest.mark.parametrize("kind", FILTERS)
def test_alpha_palette_with_transparency(kind):
    width, height = 7, 5
    indices = [bytes((x + y) % 4 for x in range(width)) for y in range(height)]
    palette = bytes(range(12))
    transparency = bytes([0, 128])  # entries 2 and 3 have no tRNS byte: opaque
    data = encode(width, height, PALETTE, 8, indices, filters=(kind,), extra=((b"PLTE", palette), (b"tRNS", transparency)))
    expected = {0: 0, 1: 128, 2: 255, 3: 255}
    assert read_alpha(data).pixels == bytes(expected[i] for row in indices for i in row)


def test_alpha_none_for_pictures_without_alpha():
    assert read_alpha(encode(4, 3, RGB, 8, pattern(4, 3, 3))) is None
    assert read_alpha(encode(4, 3, GREY, 8, pattern(4, 3, 1))) is None
    indices = [bytes(4)] * 3
    assert read_alpha(encode(4, 3, PALETTE, 8, indices, extra=((b"PLTE", bytes(6)),))) is None


def test_alpha_error_for_a_colour_key_and_a_low_bit_depth():
    with pytest.raises(AlphaError, match="colour key"):
        read_alpha(encode(4, 3, RGB, 8, pattern(4, 3, 3), extra=((b"tRNS", bytes(6)),)))
    header = struct.pack("!IIBBBBB", 4, 3, 4, PALETTE, 0, 0, 0)
    data = SIGNATURE + chunk(b"IHDR", header) + chunk(b"PLTE", bytes(6)) + chunk(b"tRNS", b"\x00") \
        + chunk(b"IDAT", zlib.compress(bytes(6))) + chunk(b"IEND", b"")
    with pytest.raises(AlphaError, match="bit depth 4"):
        read_alpha(data)


def test_alpha_refuses_interlaced():
    with pytest.raises(AlphaError, match="interlaced"):
        read_alpha(encode(4, 3, RGBA, 8, pattern(4, 3, 4), interlace=1))


def test_alpha_refuses_bad_crc():
    data = bytearray(encode(4, 3, RGBA, 8, pattern(4, 3, 4)))
    data[-20] ^= 0xFF  # inside the IDAT chunk
    with pytest.raises(AlphaError, match="CRC"):
        read_alpha(bytes(data))


def test_alpha_refuses_truncated_files():
    data = encode(4, 3, RGBA, 8, pattern(4, 3, 4))
    for cut in (len(data) - 1, len(data) - 12, len(SIGNATURE) + 5, len(SIGNATURE)):
        with pytest.raises(AlphaError, match="truncated"):
            read_alpha(data[:cut])


def test_alpha_refuses_short_and_long_image_data():
    header = struct.pack("!IIBBBBB", 4, 3, 8, RGBA, 0, 0, 0)
    for raw in (bytes(4 * 4 * 2 + 2), bytes(4 * 4 * 3 + 3 + 1)):
        data = SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
        with pytest.raises(AlphaError):
            read_alpha(data)


def test_alpha_refuses_what_is_not_a_png_and_a_bad_header():
    with pytest.raises(AlphaError, match="not a PNG"):
        read_alpha(b"GIF89a....")
    with pytest.raises(AlphaError, match="not a PNG"):
        read_alpha(b"")
    huge = struct.pack("!IIBBBBB", 100000, 100000, 8, RGBA, 0, 0, 0)
    with pytest.raises(AlphaError, match="size"):
        read_alpha(SIGNATURE + chunk(b"IHDR", huge) + chunk(b"IEND", b""))
    bad_filter = SIGNATURE + chunk(b"IHDR", struct.pack("!IIBBBBB", 1, 1, 8, RGBA, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(b"\x09\x00\x00\x00\x00")) + chunk(b"IEND", b"")
    with pytest.raises(AlphaError, match="filter type 9"):
        read_alpha(bad_filter)


def test_alpha_of_a_larger_picture_with_every_filter():
    width, height = 200, 150
    rows = pattern(width, height, 4)
    alpha = read_alpha(encode(width, height, RGBA, 8, rows, filters=FILTERS))
    assert alpha.pixels == expected_alpha(rows, width, 4)
