from conftest import COLOR_TYPE_RGB, write_png

import png_diff
from png_reader import read_png

BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)


def picture(width, height, blocks=(), base=BLACK, color=WHITE):
    """A picture of one color with filled blocks `(x0, y0, x1, y1)`, corners inclusive."""

    def pixel(x, y):
        return color if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in blocks) else base

    return read_png(write_png(width, height, pixel))


def test_identical_pictures_have_no_change_and_no_region():
    result = png_diff.compare(picture(32, 32, [(2, 2, 9, 9)]), picture(32, 32, [(2, 2, 9, 9)]))

    assert (result.changed, result.total, result.share, result.regions) == (0, 1024, 0.0, [])
    assert png_diff.describe("a.png", result) == ["a.png: unchanged"]


def test_one_changed_pixel_is_one_region_of_one_pixel():
    result = png_diff.compare(picture(32, 32), picture(32, 32, [(5, 7, 5, 7)]))

    assert result.changed == 1 and result.region_count == 1
    assert result.regions == [png_diff.Region(5, 7, 5, 7, 1)]
    assert png_diff.describe("a.png", result) == [
        "a.png: 0.10% changed (1 of 1024 px), 1 region",
        "  [5,7,5,7] 0.10% of the picture, 100% changed inside",
    ]


def test_a_changed_block_gives_one_box_with_the_exact_corners():
    result = png_diff.compare(picture(64, 64), picture(64, 64, [(10, 12, 29, 21)]))

    assert result.changed == 20 * 10
    assert result.regions == [png_diff.Region(10, 12, 29, 21, 200)]
    assert png_diff.describe("scene.png", result) == [
        "scene.png: 4.88% changed (200 of 4096 px), 1 region",
        "  [10,12,29,21] 4.88% of the picture, 100% changed inside",
    ]


def test_two_far_blocks_give_two_boxes_the_larger_first():
    result = png_diff.compare(picture(128, 64), picture(128, 64, [(2, 2, 9, 9), (100, 40, 119, 59)]))

    assert result.region_count == 2
    assert [(r.x0, r.y0, r.x1, r.y1, r.changed) for r in result.regions] == [(100, 40, 119, 59, 400), (2, 2, 9, 9, 64)]


def test_cells_that_touch_by_a_corner_are_one_region():
    # The blocks sit in cells (0,0) and (1,1) of 8 px: 8-connected, so one group; the boxes do not overlap.
    result = png_diff.compare(picture(32, 32), picture(32, 32, [(0, 0, 7, 7), (8, 8, 15, 15)]))

    assert result.region_count == 1
    assert result.regions == [png_diff.Region(0, 0, 15, 15, 128)]


def test_two_groups_whose_boxes_overlap_are_merged():
    # An L of changed pixels and a lone block inside the L's box: separate groups of cells, but their boxes overlap.
    frame = [(0, 0, 63, 3), (0, 4, 3, 63)]
    result = png_diff.compare(picture(96, 96), picture(96, 96, frame + [(40, 40, 41, 41)]))

    assert result.region_count == 1
    assert (result.regions[0].x0, result.regions[0].y0, result.regions[0].x1, result.regions[0].y1) == (0, 0, 63, 63)
    assert result.regions[0].changed == result.changed

    apart = png_diff.compare(picture(96, 96), picture(96, 96, frame + [(80, 80, 81, 81)]))
    assert apart.region_count == 2


def test_tolerance_hides_a_difference_of_two_and_shows_one_of_forty():
    base = picture(16, 16)
    faint = picture(16, 16, [(0, 0, 3, 3)], color=(2, 0, 0, 255))
    strong = picture(16, 16, [(0, 0, 3, 3)], color=(40, 0, 0, 255))

    assert png_diff.compare(base, faint).changed == 16
    assert png_diff.compare(base, faint, tolerance=2).changed == 0
    assert png_diff.compare(base, strong, tolerance=2).changed == 16
    assert png_diff.compare(base, strong, tolerance=39).changed == 16
    assert png_diff.compare(base, strong, tolerance=40).changed == 0


def test_a_pixel_with_several_differing_channels_counts_once():
    result = png_diff.compare(picture(8, 8), picture(8, 8, [(1, 1, 1, 1)]))  # white vs black: three channels differ

    assert result.changed == 1


def test_only_the_five_largest_regions_are_reported():
    blocks = [(x * 16, 0, x * 16 + side - 1, side - 1) for x, side in enumerate([2, 3, 4, 5, 6, 7, 8])]
    result = png_diff.compare(picture(128, 16), picture(128, 16, blocks))

    assert result.region_count == 7
    assert [r.changed for r in result.regions] == [64, 49, 36, 25, 16]
    lines = png_diff.describe("a.png", result)
    assert lines[0].endswith("7 regions") and len(lines) == 1 + png_diff.MAX_REGIONS


def test_another_size_is_a_size_change_and_not_an_error():
    result = png_diff.compare(picture(64, 36), picture(36, 64))

    assert isinstance(result, png_diff.SizeChange)
    assert png_diff.describe("a.png", result) == ["a.png: size 64x36 -> 36x64, not compared"]


def test_another_channel_count_is_a_size_change():
    rgb = read_png(write_png(8, 8, lambda x, y: (0, 0, 0), color_type=COLOR_TYPE_RGB))
    result = png_diff.compare(picture(8, 8), rgb)

    assert isinstance(result, png_diff.SizeChange)
    assert png_diff.describe("a.png", result) == ["a.png: channels 4 -> 3, not compared"]
    assert png_diff.describe("a.png", png_diff.compare(rgb, picture(8, 8))) == ["a.png: channels 3 -> 4, not compared"]
