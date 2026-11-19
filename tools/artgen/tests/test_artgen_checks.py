"""The checks of a cut-out (tools/artgen/CLAUDE.md, "The service"): the PNG alpha reader and, below it, the checks over the alpha."""

import json
import math
import struct
import sys
import zlib
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen_checks  # noqa: E402
from artgen_alpha import Alpha, AlphaError, read_alpha  # noqa: E402
from artgen_checks import DEFAULTS, check_alpha, check_png  # noqa: E402

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


# ---- the checks over drawn masks ----

SIDE = 200


def draw(side, *shapes):
    """An Alpha of `side` squared drawn by the shapes: each is a function (x, y) -> alpha or None; the last that answers wins."""
    pixels = bytearray(side * side)
    for y in range(side):
        for x in range(side):
            for shape in shapes:
                value = shape(x, y)
                if value is not None:
                    pixels[y * side + x] = value
    return Alpha(side, side, bytes(pixels))


def disc(cx, cy, radius, alpha=255):
    return lambda x, y: alpha if math.hypot(x - cx, y - cy) <= radius else None


def square(left, top, right, bottom, alpha=255):
    return lambda x, y: alpha if left <= x < right and top <= y < bottom else None


def ring(cx, cy, inner, outer, alpha):
    return lambda x, y: alpha if inner < math.hypot(x - cx, y - cy) <= outer else None


def failed_of(alpha, thresholds=None):
    return check_alpha(alpha, thresholds)["failed"]


def test_checks_a_clean_disc_passes():
    verdict = check_alpha(draw(SIDE, disc(100, 100, 60)))
    assert verdict["passed"] is True
    assert verdict["failed"] == []
    assert verdict["thresholds"] == DEFAULTS
    assert 0.27 < verdict["metrics"]["fill"] < 0.30
    assert verdict["metrics"]["pieces"] == 1
    assert verdict["metrics"]["halo"] == 0.0


def test_checks_a_soft_edge_of_a_few_pixels_passes():
    assert failed_of(draw(SIDE, ring(100, 100, 60, 63, 60), disc(100, 100, 60))) == []


def test_checks_empty():
    assert failed_of(Alpha(SIDE, SIDE, bytes(SIDE * SIDE))) == ["empty"]
    assert failed_of(draw(SIDE, disc(100, 100, 60, alpha=100))) == ["empty"]


def test_checks_an_object_cut_by_a_frame_edge():
    assert failed_of(draw(SIDE, disc(100, 50, 60))) == ["edge"]
    assert failed_of(draw(SIDE, disc(50, 100, 60))) == ["edge"]
    assert failed_of(draw(SIDE, disc(100, 150, 60))) == ["edge"]
    assert failed_of(draw(SIDE, disc(150, 100, 60))) == ["edge"]


def test_checks_a_disc_that_only_touches_an_edge_is_not_cut():
    verdict = check_alpha(draw(SIDE, disc(100, 60, 60)))
    assert "edge" not in verdict["failed"]


def test_checks_two_pieces_and_a_speck():
    two = draw(SIDE, disc(60, 100, 30), disc(140, 100, 30))
    assert failed_of(two) == ["pieces"]
    speck = draw(SIDE, disc(100, 100, 60), disc(10, 10, 1))
    assert failed_of(speck) == []
    assert check_alpha(speck)["metrics"]["pieces"] == 1


def test_checks_pieces_touching_at_a_corner_are_two():
    touching = draw(SIDE, square(40, 40, 100, 100), square(100, 100, 160, 160))
    assert check_alpha(touching)["metrics"]["pieces"] == 2


def test_checks_a_wide_soft_ring_is_a_halo():
    verdict = check_alpha(draw(SIDE, ring(100, 100, 50, 80, 100), disc(100, 100, 50)))
    assert verdict["failed"] == ["halo"]
    assert verdict["metrics"]["halo"] > 0.5


def test_checks_a_background_that_was_not_removed():
    assert failed_of(draw(SIDE, square(3, 3, SIDE - 3, SIDE - 3))) == ["background"]
    # Opaque corners around a hole: the fill is below the limit, the corners say the same.
    framed = draw(SIDE, square(0, 0, SIDE, SIDE), lambda x, y: 0 if math.hypot(x - 100, y - 100) <= 70 else None)
    assert "background" in failed_of(framed)
    assert check_alpha(framed)["metrics"]["fill"] < DEFAULTS["maxFill"]


def test_checks_a_dot_is_small():
    assert failed_of(draw(SIDE, disc(100, 100, 5))) == ["small"]


def test_checks_an_object_in_a_corner_is_off_centre():
    assert failed_of(draw(SIDE, disc(50, 50, 40))) == ["offCentre"]


def test_checks_a_picture_larger_than_the_reduced_side_gives_the_same_verdicts():
    big = 600
    assert failed_of(draw(big, disc(300, 300, 180))) == []
    assert failed_of(draw(big, disc(180, 300, 90), disc(420, 300, 90))) == ["pieces"]
    assert failed_of(draw(big, disc(300, 150, 180))) == ["edge"]
    assert failed_of(draw(big, disc(300, 300, 15))) == ["small"]
    verdict = check_alpha(draw(big, disc(300, 300, 180)))
    assert verdict["metrics"]["fill"] == pytest.approx(math.pi * 180 ** 2 / big ** 2, abs=0.01)
    assert verdict["metrics"]["cover"] == pytest.approx(0.36, abs=0.02)


def test_checks_a_picture_whose_side_is_not_a_multiple_of_the_scale():
    alpha = Alpha(301, 199, bytes([255]) * (301 * 199))
    assert "background" in failed_of(alpha)


def test_checks_each_threshold_can_be_replaced():
    clean = draw(SIDE, disc(100, 100, 60))
    assert failed_of(clean, {"maxFill": 0.1}) == ["background"]
    assert failed_of(clean, {"minCover": 0.5}) == ["small"]
    assert failed_of(draw(SIDE, disc(100, 100, 60, alpha=200)), {"cut": 201}) == ["empty"]
    two = draw(SIDE, disc(60, 100, 30), disc(140, 100, 30))
    assert failed_of(two, {"maxPieces": 2}) == ["bodies"], "a gap is a valley too once two pieces are allowed"
    assert failed_of(two, {"maxPieces": 2, "maxValley": 1}) == []
    speck = draw(SIDE, disc(100, 100, 60), disc(10, 10, 3))
    assert failed_of(speck, {"minPieceArea": 0.0001}) == ["pieces"]
    assert failed_of(draw(SIDE, disc(100, 50, 60)), {"maxEdgeRun": 0.5}) == []
    assert failed_of(draw(SIDE, disc(50, 50, 40)), {"maxOffset": 0.5}) == []
    assert failed_of(draw(SIDE, ring(100, 100, 50, 80, 100), disc(100, 100, 50)), {"maxHalo": 0.9}) == []
    verdict = check_alpha(clean, {"maxFill": 0.5})
    assert verdict["thresholds"]["maxFill"] == 0.5 and verdict["thresholds"]["cut"] == DEFAULTS["cut"]


def two_columns_on_a_base(scale, base_top):
    """Two columns standing on a wide base: the base is one body's floor, the columns are two bodies when it is thin."""
    side = SIDE * scale
    return draw(side,
                square(30 * scale, base_top * scale, 170 * scale, 175 * scale),
                square(40 * scale, 60 * scale, 80 * scale, base_top * scale),
                square(120 * scale, 60 * scale, 160 * scale, base_top * scale))


def test_checks_a_single_body_has_no_valley():
    verdict = check_alpha(draw(SIDE, disc(100, 100, 60)))
    assert verdict["metrics"]["valley"] == 0.0
    handled = check_alpha(draw(SIDE, disc(90, 100, 50), ring(150, 100, 15, 22, 255)))
    assert handled["failed"] == [] and handled["metrics"]["valley"] < 0.1


def test_checks_two_bodies_joined_by_a_thin_base_fail_bodies_and_not_pieces():
    verdict = check_alpha(two_columns_on_a_base(1, 155))
    assert verdict["failed"] == ["bodies"]
    assert verdict["metrics"]["pieces"] == 1
    assert verdict["metrics"]["valley"] == pytest.approx(0.83, abs=0.02)


def test_checks_a_thick_bridge_is_one_object():
    thick = draw(SIDE, square(30, 105, 170, 175), square(40, 60, 80, 105), square(120, 60, 160, 105))
    verdict = check_alpha(thick)
    assert verdict["failed"] == []
    assert verdict["metrics"]["valley"] == pytest.approx(0.39, abs=0.02)


def test_checks_two_discs_apart_fail_pieces_only():
    verdict = check_alpha(draw(SIDE, disc(60, 100, 30), disc(140, 100, 30)))
    assert verdict["failed"] == ["pieces"]
    assert verdict["metrics"]["valley"] == 1.0


def test_checks_narrow_teeth_of_a_crown_are_not_bodies():
    teeth = [square(x, 70, x + 3, 130) for x in (40, 70, 100, 130, 157)]
    crown = draw(SIDE, square(40, 130, 160, 150), *teeth)
    assert failed_of(crown) == []
    assert failed_of(crown, {"minBodyWidth": 0.001}) == ["bodies"], "without the opening each tooth is a body"


def test_checks_a_thin_tall_antenna_is_not_a_body():
    antenna = draw(SIDE, disc(80, 120, 50), square(95, 166, 153, 171), square(150, 40, 153, 166))
    verdict = check_alpha(antenna)
    assert verdict["failed"] == []
    assert verdict["metrics"]["pieces"] == 1 and verdict["metrics"]["valley"] == 0.0
    assert failed_of(antenna, {"minBodyWidth": 0.001}) == ["bodies"]


def test_checks_a_picture_larger_than_the_reduced_side_has_the_same_valley():
    small = check_alpha(two_columns_on_a_base(1, 155))
    big = check_alpha(two_columns_on_a_base(3, 155))
    assert big["failed"] == ["bodies"]
    assert big["metrics"]["valley"] == pytest.approx(small["metrics"]["valley"], abs=0.03)
    assert failed_of(draw(600, square(90, 105 * 3, 510, 175 * 3), square(120, 180, 240, 315), square(360, 180, 480, 315))) == []


def test_checks_both_valley_thresholds_can_be_replaced():
    joined = two_columns_on_a_base(1, 155)
    assert failed_of(joined, {"maxValley": 0.9}) == []
    assert failed_of(joined, {"maxValley": 0.0}) == ["bodies"]
    assert failed_of(joined, {"minBodyWidth": 0.6}) == []
    verdict = check_alpha(joined, {"maxValley": 0.9, "minBodyWidth": 0.1})
    assert verdict["thresholds"]["maxValley"] == 0.9 and verdict["thresholds"]["minBodyWidth"] == 0.1


def test_checks_the_names_of_the_thresholds_and_of_the_checks():
    assert set(DEFAULTS) == {"cut", "maxEdgeRun", "maxPieces", "minPieceArea", "maxValley", "minBodyWidth", "maxHalo", "maxFill",
                             "minCover", "maxOffset"}
    assert set(artgen_checks.CHECK_NAMES) == {"empty", "edge", "pieces", "bodies", "halo", "background", "small", "offCentre"}


# Silhouettes of the Chess game's art, reduced to a 0/255 alpha (no colour): `accept/` holds pictures that are one piece (the
# game's accepted set, a horn or a glass on a stand), `reject/` pictures of several objects that stand together on a thin base.
SILHOUETTES = Path(__file__).resolve().parent / "fixtures" / "silhouettes"


def silhouettes(kind):
    return sorted((SILHOUETTES / kind).glob("*.png"))


@pytest.mark.parametrize("path", silhouettes("accept"), ids=lambda path: path.stem)
def test_checks_the_accepted_silhouettes_pass(path):
    verdict = check_png(path.read_bytes())
    assert verdict["failed"] == [], verdict["metrics"]


def png_of(alpha):
    rows = []
    for y in range(alpha.height):
        row = bytearray()
        for value in alpha.pixels[y * alpha.width:(y + 1) * alpha.width]:
            row += bytes([20, 40, 60, value])
        rows.append(bytes(row))
    return encode(alpha.width, alpha.height, RGBA, 8, rows)


def test_check_png_gives_the_verdict_of_the_alpha_in_plain_data():
    clean = check_png(png_of(draw(SIDE, disc(100, 100, 60))))
    assert clean["passed"] is True
    cut_off = check_png(png_of(draw(SIDE, disc(100, 50, 60))))
    assert cut_off["passed"] is False and cut_off["failed"] == ["edge"]
    assert json.loads(json.dumps(cut_off)) == cut_off
    assert check_png(png_of(draw(SIDE, disc(100, 100, 60))), {"minCover": 0.9})["failed"] == ["small"]


def test_check_png_none_without_alpha_and_an_error_for_a_picture_that_cannot_be_read():
    assert check_png(encode(4, 3, RGB, 8, pattern(4, 3, 3))) is None
    assert check_png(b"not a picture") == {"passed": None, "error": "not a PNG picture"}
    truncated = png_of(draw(SIDE, disc(100, 100, 60)))[:-30]
    verdict = check_png(truncated)
    assert verdict["passed"] is None and "truncated" in verdict["error"]
