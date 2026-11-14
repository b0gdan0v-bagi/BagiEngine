import json

import pytest
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


def test_a_region_hides_a_change_outside_it_and_keeps_whole_picture_coordinates():
    base = picture(64, 64)
    changed = picture(64, 64, [(2, 2, 5, 5), (40, 40, 49, 44)])

    result = png_diff.compare(base, changed, region=[32, 32, 63, 63])

    assert result.changed == 50 and result.total == 32 * 32
    assert result.regions == [png_diff.Region(40, 40, 49, 44, 50)]
    assert result.bbox == (40, 40, 49, 44)


def test_the_bbox_is_the_exact_box_of_all_changed_pixels_and_none_without_a_change():
    result = png_diff.compare(picture(64, 64), picture(64, 64, [(3, 5, 3, 5), (60, 50, 60, 50)]))
    assert result.bbox == (3, 5, 60, 50)
    assert png_diff.compare(picture(8, 8), picture(8, 8)).bbox is None


@pytest.mark.parametrize("region", [[0, 0, 8, 7], [-1, 0, 3, 3], [4, 0, 3, 3], [0, 4, 3, 3], [0, 0, 3], [0, 0, 1.5, 2], "abcd"])
def test_a_bad_region_is_a_value_error(region):
    with pytest.raises(ValueError, match="region"):
        png_diff.compare(picture(8, 8), picture(8, 8), region=region)


def test_the_map_is_grey_of_a_with_red_where_changed_and_only_grey_outside_the_region():
    a = picture(8, 8, base=(100, 100, 100, 255))
    b = picture(8, 8, [(1, 1, 1, 1), (6, 6, 6, 6)], base=(100, 100, 100, 255))

    full = png_diff.diff_map(a, b)
    assert full.channels == 3 and (full.width, full.height) == (8, 8)
    assert full.pixel(1, 1) == (255, 0, 0) and full.pixel(6, 6) == (255, 0, 0)
    assert full.pixel(0, 0) == (50, 50, 50)

    inside = png_diff.diff_map(a, b, region=[0, 0, 3, 3])
    assert inside.pixel(1, 1) == (255, 0, 0) and inside.pixel(6, 6) == (50, 50, 50)

    assert png_diff.diff_map(a, b, tolerance=255).colors() == {(50, 50, 50)}


def test_a_map_of_two_different_pictures_is_refused():
    with pytest.raises(ValueError, match="one size"):
        png_diff.diff_map(picture(8, 8), picture(8, 4))


def test_image_diff_scales_b_to_a_and_says_so():
    a = picture(16, 16, [(0, 0, 7, 15)])
    b = picture(8, 8, [(0, 0, 3, 7)])  # the same picture at half size

    result = png_diff.image_diff(a, b)

    assert result.scaled and not result.aspect_differs
    assert result.comparison.total == 256
    assert result.map.width == 16
    assert result.comparison.changed < 40  # only the blurred edge differs


def test_image_diff_reports_a_stretch_when_the_shapes_differ():
    result = png_diff.image_diff(picture(16, 16), picture(8, 16))
    assert result.scaled and result.aspect_differs and result.stats()["aspectDiffers"] is True


def test_image_diff_compares_rgb_with_rgba_on_rgb():
    rgba = picture(8, 8, [(2, 2, 2, 2)])
    rgb = read_png(write_png(8, 8, lambda x, y: (255, 255, 255) if (x, y) == (2, 2) else (0, 0, 0), color_type=COLOR_TYPE_RGB))

    result = png_diff.image_diff(rgba, rgb)

    assert result.comparison.changed == 0 and not result.scaled
    assert result.stats()["a"]["channels"] == 4 and result.stats()["b"]["channels"] == 3
    assert result.map.channels == 3


def test_image_diff_uses_the_threshold_and_the_region_and_checks_them():
    a = picture(16, 16)
    b = picture(16, 16, [(1, 1, 2, 2), (10, 10, 12, 12)], color=(30, 0, 0, 255))

    assert png_diff.image_diff(a, b).comparison.changed == 4 + 9
    assert png_diff.image_diff(a, b, threshold=30).comparison.changed == 0
    inside = png_diff.image_diff(a, b, region=[8, 8, 15, 15])
    assert inside.comparison.changed == 9 and inside.comparison.total == 64
    with pytest.raises(ValueError, match="region"):
        png_diff.image_diff(a, b, region=[0, 0, 16, 16])
    for threshold in (-1, 256, 1.5, "3", True):
        with pytest.raises(ValueError, match="threshold"):
            png_diff.image_diff(a, b, threshold=threshold)


def test_stats_has_the_documented_keys_and_is_json():
    a = picture(32, 32)
    b = picture(32, 32, [(4, 4, 11, 11)])

    stats = png_diff.image_diff(a, b, threshold=1, region=[0, 0, 31, 15]).stats()

    assert set(stats) == {"equal", "changed", "compared", "share", "boundingBox", "regions", "threshold", "region", "a", "b", "scaled", "aspectDiffers"}
    assert stats["equal"] is False and stats["changed"] == 64 and stats["compared"] == 32 * 16
    assert stats["share"] == 64 / 512 and stats["boundingBox"] == [4, 4, 11, 11]
    assert stats["regions"] == [{"box": [4, 4, 11, 11], "changed": 64, "shareOfPicture": 64 / 1024}]
    assert stats["threshold"] == 1 and stats["region"] == [0, 0, 31, 15] and stats["scaled"] is False
    assert stats["a"] == {"width": 32, "height": 32, "channels": 4}
    assert json.loads(json.dumps(stats)) == stats

    same = png_diff.image_diff(a, a).stats()
    assert same["equal"] is True and same["boundingBox"] is None and same["regions"] == [] and same["region"] is None
