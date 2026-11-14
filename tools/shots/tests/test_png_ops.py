import time

import pytest
from conftest import COLOR_TYPE_RGB, write_png

import png_ops
from png_reader import PngImage, read_png


def gradient(width, height, color_type=6):
    return read_png(write_png(width, height, lambda x, y: (x * 10 % 256, y * 10 % 256, 7, 200)[: 4 if color_type == 6 else 3], color_type))


def test_crop_takes_the_box_with_inclusive_corners():
    cropped = png_ops.crop(gradient(8, 8), 2, 3, 4, 3)

    assert (cropped.width, cropped.height, cropped.channels) == (3, 1, 4)
    assert cropped.pixel(0, 0) == (20, 30, 7, 200) and cropped.pixel(2, 0) == (40, 30, 7, 200)


@pytest.mark.parametrize("box", [(-1, 0, 2, 2), (0, 0, 8, 2), (0, 0, 2, 8), (3, 0, 2, 2), (0, 3, 2, 2)])
def test_crop_outside_the_picture_or_the_wrong_way_round_is_an_error(box):
    with pytest.raises(ValueError, match="not inside"):
        png_ops.crop(gradient(8, 8), *box)


def test_to_channels_drops_the_alpha_and_keeps_the_color():
    rgb = png_ops.to_channels(gradient(4, 2), 3)

    assert (rgb.width, rgb.height, rgb.channels) == (4, 2, 3)
    assert rgb.pixel(3, 1) == (30, 10, 7)
    assert all(len(row) == 12 for row in rgb.rows)


def test_to_channels_of_the_same_count_is_the_same_picture_and_alpha_cannot_be_added():
    image = gradient(4, 2)
    assert png_ops.to_channels(image, 4) is image
    with pytest.raises(ValueError, match="cannot get 4"):
        png_ops.to_channels(png_ops.to_channels(image, 3), 4)
    with pytest.raises(ValueError, match="3 or 4"):
        png_ops.to_channels(image, 2)


def test_scale_to_the_same_size_changes_nothing():
    image = gradient(5, 4)
    assert png_ops.scale_to(image, 5, 4) is image


def test_scale_up_blends_the_neighbours_and_keeps_the_ends():
    image = read_png(write_png(2, 1, lambda x, y: (0, 0, 0, 255) if x == 0 else (200, 100, 50, 255)))

    scaled = png_ops.scale_to(image, 4, 2)

    assert (scaled.width, scaled.height) == (4, 2)
    assert scaled.pixel(0, 0) == (0, 0, 0, 255) and scaled.pixel(3, 1) == (200, 100, 50, 255)
    middle = [scaled.pixel(1, 0)[0], scaled.pixel(2, 0)[0]]
    assert 0 < middle[0] < middle[1] < 200
    assert scaled.rows[0] == scaled.rows[1]  # one source row: every output row is the same


def test_scale_down_of_a_flat_picture_stays_flat_and_a_half_blends_both_halves():
    flat = read_png(write_png(10, 10, lambda x, y: (9, 8, 7, 255)))
    assert set(png_ops.scale_to(flat, 3, 4).colors()) == {(9, 8, 7, 255)}

    halves = read_png(write_png(4, 4, lambda x, y: (0, 0, 0) if x < 2 else (100, 100, 100), COLOR_TYPE_RGB))
    one = png_ops.scale_to(halves, 1, 1)
    assert one.pixel(0, 0) == (50, 50, 50)


def test_scale_to_a_bad_size_is_an_error():
    with pytest.raises(ValueError, match="cannot be scaled"):
        png_ops.scale_to(gradient(4, 4), 0, 4)


def test_a_640_picture_scales_in_a_second_or_two():
    row = (bytes(range(256)) * 8)[: 640 * 3]
    image = PngImage(640, 640, 3, [row] * 640)

    started = time.monotonic()
    png_ops.scale_to(image, 400, 300)
    png_ops.scale_to(image, 800, 700)

    assert time.monotonic() - started < 10.0


@pytest.mark.parametrize("color_type", [COLOR_TYPE_RGB, 6])
def test_encode_png_round_trips(color_type):
    image = gradient(7, 5, color_type)
    decoded = read_png(png_ops.encode_png(image))

    assert decoded == image


def test_encode_png_refuses_a_picture_it_cannot_write():
    with pytest.raises(ValueError, match="3 or 4"):
        png_ops.encode_png(PngImage(1, 1, 1, [b"\x00"]))
