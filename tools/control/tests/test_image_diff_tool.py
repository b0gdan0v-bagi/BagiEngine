"""The `image_diff` tool (bagi_image.py) on two files and on the picture of a fake game."""

import base64
import shutil
import subprocess

import pytest

from bagi_image import PREVIEW_SIZE, ImageError, run_image_diff  # first: it puts tools/shots on sys.path for png_ops
from bagi_session import GameSession, SessionError
from fake_control_server import FakeControlServer
from png_reader import PngImage, read_png

import png_ops  # noqa: E402  (after bagi_image, which puts tools/shots on sys.path)

RED = (255, 0, 0)


def picture(width, height, blocks=(), base=(0, 0, 0)):
    """An RGB picture of one color with red blocks `(x0, y0, x1, y1)`, corners inclusive."""
    rows = [bytes(b for x in range(width) for b in (RED if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in blocks) else base)) for y in range(height)]
    return PngImage(width, height, 3, rows)


def save(path, image):
    path.write_bytes(png_ops.encode_png(image))
    return str(path)


@pytest.fixture
def session():
    game = GameSession()
    yield game
    game.close()


def game_serving(image):
    """A fake control server whose `screenshot` answers `image`; the call parameters it got are in `.requests`."""

    def handler(method, params):
        if method == "status":
            return {"frame": 1, "manualClock": True}
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "screenshot":
            return {"width": image.width, "height": image.height, "png_base64": base64.b64encode(png_ops.encode_png(image)).decode("ascii")}
        raise AssertionError(method)

    server = FakeControlServer(handler)
    server.port = server.start()
    return server


def test_two_equal_files_are_equal_and_the_map_is_all_grey(session, tmp_path):
    a = save(tmp_path / "a.png", picture(8, 4, [(1, 1, 2, 2)]))
    b = save(tmp_path / "b.png", picture(8, 4, [(1, 1, 2, 2)]))

    stats, preview = run_image_diff(session, a, b, "", 0, None)

    assert stats["equal"] is True and stats["changed"] == 0 and stats["boundingBox"] is None
    assert "out" not in stats
    assert read_png(preview).colors() <= {(0, 0, 0), (38, 38, 38)}  # the grey of black and of red at half brightness


def test_two_different_files_give_stats_and_a_red_map(session, tmp_path):
    a = save(tmp_path / "a.png", picture(8, 4))
    b = save(tmp_path / "b.png", picture(8, 4, [(3, 1, 4, 2)]))

    stats, preview = run_image_diff(session, a, b, "", 0, None)

    assert stats["equal"] is False and stats["changed"] == 4 and stats["boundingBox"] == [3, 1, 4, 2]
    assert read_png(preview).pixel(3, 1) == RED


def test_threshold_and_region_reach_the_comparison(session, tmp_path):
    a = save(tmp_path / "a.png", picture(8, 4))
    b = save(tmp_path / "b.png", picture(8, 4, [(0, 0, 0, 0), (6, 3, 6, 3)]))

    assert run_image_diff(session, a, b, "", 0, [4, 0, 7, 3])[0]["changed"] == 1
    assert run_image_diff(session, a, b, "", 255, None)[0]["equal"] is True


def test_the_home_folder_is_expanded_in_a_path(session, tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    save(tmp_path / "a.png", picture(2, 2))

    assert run_image_diff(session, "~/a.png", "~/a.png", "", 0, None)[0]["equal"] is True


def test_the_out_file_holds_the_full_size_map_and_is_named_in_the_stats(session, tmp_path):
    a = save(tmp_path / "a.png", picture(8, 4))
    b = save(tmp_path / "b.png", picture(8, 4, [(0, 0, 1, 1)]))
    out = tmp_path / "map.png"

    stats, _ = run_image_diff(session, a, b, str(out), 0, None)

    assert stats["out"] == str(out)
    written = read_png(out.read_bytes())
    assert (written.width, written.height) == (8, 4) and written.pixel(0, 0) == RED


def test_out_in_a_folder_that_does_not_exist_is_refused(session, tmp_path):
    a = save(tmp_path / "a.png", picture(2, 2))

    with pytest.raises(ImageError, match="does not exist"):
        run_image_diff(session, a, a, str(tmp_path / "nowhere" / "map.png"), 0, None)


@pytest.mark.skipif(shutil.which("git") is None, reason="needs git")
def test_out_inside_a_git_work_tree_is_refused_and_nothing_is_written(session, tmp_path):
    repo = tmp_path / "repo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    a = save(tmp_path / "a.png", picture(2, 2))

    with pytest.raises(ImageError, match="inside a git work tree"):
        run_image_diff(session, a, a, str(repo / "map.png"), 0, None)

    assert not (repo / "map.png").exists()


def test_the_preview_is_shrunk_to_the_cap_and_a_small_one_is_not_enlarged(session, tmp_path):
    big = save(tmp_path / "big.png", picture(PREVIEW_SIZE * 2, 8))
    small = save(tmp_path / "small.png", picture(16, 8))

    shown = read_png(run_image_diff(session, big, big, "", 0, None)[1])
    assert shown.width == PREVIEW_SIZE and 1 <= shown.height < 8

    kept = read_png(run_image_diff(session, small, small, "", 0, None)[1])
    assert (kept.width, kept.height) == (16, 8)


def test_a_different_size_is_scaled_and_said_so(session, tmp_path):
    a = save(tmp_path / "a.png", picture(8, 8))
    b = save(tmp_path / "b.png", picture(4, 4))

    stats, _ = run_image_diff(session, a, b, "", 0, None)

    assert stats["scaled"] is True and stats["aspectDiffers"] is False and stats["equal"] is True


def test_screen_is_the_picture_of_the_connected_game_at_full_size(session, tmp_path):
    server = game_serving(picture(8, 4, [(0, 0, 1, 1)]))
    try:
        session.connect(port=server.port)
        reference = save(tmp_path / "ref.png", picture(8, 4))

        stats, _ = run_image_diff(session, reference, "screen", "", 0, None)

        assert stats["changed"] == 4 and stats["b"] == {"width": 8, "height": 4, "channels": 3}
        assert [params for name, params in server.requests if name == "screenshot"] == [{"maxSize": 4096}]
    finally:
        server.stop()


def test_screen_with_no_game_is_a_session_error(session, tmp_path):
    a = save(tmp_path / "a.png", picture(2, 2))

    with pytest.raises(SessionError, match="launch_game or connect"):
        run_image_diff(session, "screen", a, "", 0, None)


def test_an_unreadable_or_damaged_file_is_an_image_error_that_names_it(session, tmp_path):
    good = save(tmp_path / "a.png", picture(2, 2))
    damaged = tmp_path / "damaged.png"
    damaged.write_bytes(b"not a picture")

    with pytest.raises(ImageError, match="missing.png"):
        run_image_diff(session, good, str(tmp_path / "missing.png"), "", 0, None)
    with pytest.raises(ImageError, match="damaged.png.*not a PNG"):
        run_image_diff(session, str(damaged), good, "", 0, None)
    with pytest.raises(ImageError, match="empty"):
        run_image_diff(session, "", good, "", 0, None)


def test_a_bad_region_or_threshold_is_an_image_error(session, tmp_path):
    a = save(tmp_path / "a.png", picture(4, 4))

    with pytest.raises(ImageError, match="region"):
        run_image_diff(session, a, a, "", 0, [0, 0, 4, 4])
    with pytest.raises(ImageError, match="threshold"):
        run_image_diff(session, a, a, "", 300, None)
