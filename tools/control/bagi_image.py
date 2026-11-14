"""The `image_diff` tool behind the MCP server: two pictures compared, the numbers and a difference map. No MCP code, so it is tested without the `mcp` package.

The comparison itself is `tools/shots/png_diff.py` (`tools/shots/CLAUDE.md`, "Comparing two pictures"); this file reads the operands (a PNG file, or the
connected game's picture), writes the map when asked and shrinks it for the model to look at.
"""

import sys
from pathlib import Path
from typing import Any, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shots"))

import png_diff  # noqa: E402  (also puts the PNG reader on sys.path)
import png_ops  # noqa: E402
from bagi_session import GameSession  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402

SCREEN = "screen"  # the operand that means the picture of the connected game now
SCREEN_MAX_SIZE = 4096  # the largest picture the game gives (its own cut): `screen` is compared at full size, not at the default 1024
PREVIEW_SIZE = 1024  # the longer side of the map that is shown to the model


class ImageError(Exception):
    """An operand that cannot be read or a request that cannot be served; the message says what is wrong."""


def _read_file(operand: str) -> PngImage:
    path = Path(operand).expanduser()
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ImageError(f"cannot read {operand!r}: {error.strerror or error}") from error
    try:
        return read_png(data)
    except PngError as error:
        raise ImageError(f"{operand!r} is not a PNG the tool can read: {error}") from error


def load_operand(session: GameSession, operand: str, which: str) -> PngImage:
    """A picture from a file (`~` expanded, a relative path from where the server runs) or, for `screen`, from the connected game at full size."""
    if not operand:
        raise ImageError(f"{which} is empty: give a PNG file or {SCREEN!r}")
    if operand != SCREEN:
        return _read_file(operand)
    png, _size = session.screenshot(max_size=SCREEN_MAX_SIZE)  # a SessionError when no game is connected, which the tool reports as it is
    try:
        return read_png(png)
    except PngError as error:
        raise ImageError(f"the picture of the game cannot be read: {error}") from error


def _checked_target(out: str) -> Path:
    """Where the map goes, checked before any work: the folder exists and is outside every git work tree (a picture is never committed)."""
    target = Path(out).expanduser()
    if not target.parent.is_dir():
        raise ImageError(f"the folder of out {out!r} does not exist")
    if png_ops.inside_git_work_tree(target.parent):
        raise ImageError(f"out {out!r} is inside a git work tree: pictures must stay outside every repository")
    return target


def run_image_diff(
    session: GameSession, a: str, b: str, out: str = "", threshold: int = 0, region: Sequence[int] | None = None
) -> tuple[dict[str, Any], bytes]:
    """Compares `b` with `a` (the reference side) and answers the stats and the PNG of the difference map shrunk to `PREVIEW_SIZE`.

    `out`, when given, is the file the full-size map is written to (its folder must exist and be outside every git work tree); the stats then carry `out`.
    A file that cannot be read, a bad region or a bad threshold is an `ImageError`; `screen` with no game connected is a `SessionError`.
    """
    target = _checked_target(out) if out else None
    first = load_operand(session, a, "a")
    second = load_operand(session, b, "b")
    try:
        result = png_diff.image_diff(first, second, threshold, region)
    except ValueError as error:
        raise ImageError(str(error)) from error
    stats = result.stats()
    if target:
        try:
            target.write_bytes(png_ops.encode_png(result.map))
        except OSError as error:
            raise ImageError(f"cannot write {out!r}: {error.strerror or error}") from error
        stats["out"] = str(target)
    return stats, png_ops.encode_png(png_ops.fit_within(result.map, PREVIEW_SIZE))
