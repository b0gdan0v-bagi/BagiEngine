"""Reference pictures of a scenario (bagi_reference.py): the comparison of a screenshot with a PNG next to the scenario, without a game."""

import base64

import pytest

from bagi_reference import check_reference, default_failures_dir  # first: it puts tools/shots on sys.path for png_ops
from png_reader import PngImage, read_png
from scenario import ReferenceSpec

import png_ops  # noqa: E402  (after bagi_reference, which puts tools/shots on sys.path)

SIZE = 20
GREY = (100, 100, 100)
RED = (255, 0, 0)


def picture(blocks=(), base=GREY, size=SIZE, height=None):
    """An RGB picture of one color with blocks `(x0, y0, x1, y1, color)`, corners inclusive."""
    height = size if height is None else height
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(size):
            color = next((c for x0, y0, x1, y1, c in blocks if x0 <= x <= x1 and y0 <= y <= y1), base)
            row += bytes(color)
        rows.append(bytes(row))
    return PngImage(size, height, 3, rows)


def answer(image):
    data = png_ops.encode_png(image)
    return {"width": image.width, "height": image.height, "png_base64": base64.b64encode(data).decode("ascii")}


@pytest.fixture
def folders(tmp_path):
    base = tmp_path / "scenarios"
    (base / "references").mkdir(parents=True)
    return base, tmp_path / "failures"


def keep_reference(base, image, name="x"):
    (base / "references" / f"{name}.png").write_bytes(png_ops.encode_png(image))


def check(folders, image, update=False, **spec):
    base, failures = folders
    return check_reference(ReferenceSpec("references/x.png", **spec), answer(image), base, update, failures, "x")


def test_an_equal_picture_passes(folders):
    keep_reference(folders[0], picture())
    assert check(folders, picture()).ok
    assert not folders[1].exists()


def test_a_change_within_the_threshold_passes(folders):
    keep_reference(folders[0], picture())
    nearly = picture([(0, 0, 4, 4, (101, 99, 100))])
    assert not check(folders, nearly).ok
    assert check(folders, nearly, threshold=1).ok


def test_a_change_within_the_max_share_passes(folders):
    keep_reference(folders[0], picture())
    one_pixel = picture([(3, 3, 3, 3, RED)])
    assert check(folders, one_pixel, max_share=1 / (SIZE * SIZE)).ok
    assert not check(folders, one_pixel, max_share=0.5 / (SIZE * SIZE)).ok


def test_a_change_above_the_max_share_fails_and_writes_the_two_pictures(folders):
    keep_reference(folders[0], picture())
    changed = picture([(2, 2, 5, 5, RED)])
    checked = check(folders, changed, max_share=0.01)
    assert not checked.ok
    assert "16 of 400 px" in checked.actual and "[2, 2, 5, 5]" in checked.actual
    actual_file, diff_file = folders[1] / "x.actual.png", folders[1] / "x.diff.png"
    assert str(actual_file) in checked.actual and str(diff_file) in checked.actual
    assert read_png(actual_file.read_bytes()).rows == changed.rows  # the game's own picture
    assert read_png(diff_file.read_bytes()).pixel(3, 3) == RED
    assert folders[0] not in actual_file.parents  # outside the folder of the scenario


def test_a_different_size_fails_with_both_sizes(folders):
    keep_reference(folders[0], picture())
    checked = check(folders, picture(size=SIZE + 4, height=SIZE + 2))
    assert not checked.ok and "size 24x22, the reference is 20x20" in checked.actual
    assert (folders[1] / "x.actual.png").is_file()


def test_a_missing_reference_fails_and_says_how_to_make_it(folders):
    checked = check(folders, picture())
    assert not checked.ok and "does not exist" in checked.actual and "--update" in checked.actual


def test_a_reference_that_is_not_a_picture_fails(folders):
    (folders[0] / "references" / "x.png").write_bytes(b"not a png")
    checked = check(folders, picture())
    assert not checked.ok and "cannot be read" in checked.actual


def test_an_answer_without_a_picture_fails(folders):
    base, failures = folders
    checked = check_reference(ReferenceSpec("references/x.png"), {"width": 1}, base, False, failures, "x")
    assert not checked.ok and "no usable picture" in checked.actual


def test_a_region_hides_a_change_outside_it(folders):
    keep_reference(folders[0], picture())
    changed = picture([(15, 15, 18, 18, RED)])
    assert not check(folders, changed).ok
    assert check(folders, changed, region=[0, 0, 9, 9]).ok
    assert not check(folders, changed, region=[10, 10, 19, 19]).ok


def test_a_region_outside_the_picture_fails_with_the_reason(folders):
    keep_reference(folders[0], picture())
    checked = check(folders, picture(), region=[0, 0, 99, 9])
    assert not checked.ok and "not inside the picture" in checked.actual


def test_update_creates_the_reference_with_the_bytes_of_the_game(folders):
    image = picture([(1, 1, 3, 3, RED)])
    checked = check(folders, image, update=True)
    path = folders[0] / "references" / "x.png"
    assert checked.ok and checked.note == f"created reference {path}"
    assert path.read_bytes() == png_ops.encode_png(image)


def test_update_rewrites_a_reference_that_differs(folders):
    keep_reference(folders[0], picture())
    checked = check(folders, picture([(0, 0, 9, 19, RED)]), update=True)
    assert checked.ok and checked.note.startswith("updated reference") and "50% changed" in checked.note
    assert read_png((folders[0] / "references" / "x.png").read_bytes()).pixel(0, 0) == RED


def test_update_notes_a_new_size(folders):
    keep_reference(folders[0], picture())
    checked = check(folders, picture(size=SIZE * 2), update=True)
    assert "size 20x20 -> 40x40" in checked.note


def test_update_leaves_an_equal_file_alone(folders):
    keep_reference(folders[0], picture())
    path = folders[0] / "references" / "x.png"
    stamp = path.stat().st_mtime_ns
    checked = check(folders, picture(), update=True)
    assert checked.ok and checked.note == "" and path.stat().st_mtime_ns == stamp


def test_update_makes_the_folders(folders):
    base, failures = folders
    checked = check_reference(ReferenceSpec("deeper/still/x.png"), answer(picture()), base, True, failures, "x")
    assert checked.ok and (base / "deeper" / "still" / "x.png").is_file()


def test_the_default_failures_folder_is_in_the_temp_folder_under_the_scenario_name(tmp_path):
    folder = default_failures_dir(tmp_path / "ui_reference.jsonl")
    assert folder.name == "ui_reference" and folder.parent.name == "bagi-reference"
