"""Reference pictures of a scenario: the `reference` expectation of a `screenshot` line (tools/control/CLAUDE.md, "Playing a scenario").

The scenario player (bagi_play.py) hands the answer of the call to `check_reference`, which reads the PNG kept next to the scenario, compares the
picture with it (`tools/shots/png_diff.py`, "Comparing two pictures") and, when it differs too much, writes the actual picture and the
difference map outside the repository. This is the file that does the I/O; the expectation itself is judged by `scenario.py`.
"""

import base64
import binascii
import sys
import tempfile
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shots"))

import png_diff  # noqa: E402  (also puts the PNG reader on sys.path)
import png_ops  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402
from scenario import SCREENSHOT_PICTURE_KEY, ReferenceCheck, ReferenceSpec  # noqa: E402

FAILURES_FOLDER = "bagi-reference"  # in the temp folder: `<temp>/bagi-reference/<scenario stem>/`
PERCENT = 100.0
UPDATE_HINT = "bagi_play.py <exe> <scenario> --update"


def default_failures_dir(scenario_file: Path) -> Path:
    """Where the pictures of a failed comparison go when nobody says: the temp folder, never the repository."""
    return Path(tempfile.gettempdir()) / FAILURES_FOLDER / scenario_file.stem


def _percent(share: float) -> str:
    return f"{share * PERCENT:.4g}%"


def _size(image: PngImage) -> str:
    return f"{image.width}x{image.height}"


def _write_failure_files(failures_dir: Path, name: str, actual_png: bytes, diff: PngImage | None) -> str:
    """Writes `<name>.actual.png` (and `<name>.diff.png`) and says where; a folder that cannot be written says so instead of hiding the failure."""
    try:
        failures_dir.mkdir(parents=True, exist_ok=True)
        actual_path = failures_dir / f"{name}.actual.png"
        actual_path.write_bytes(actual_png)
        text = f"actual picture: {actual_path}"
        if diff is not None:
            diff_path = failures_dir / f"{name}.diff.png"
            diff_path.write_bytes(png_ops.encode_png(diff))
            text += f"; difference map: {diff_path}"
        return text
    except OSError as error:
        return f"(the pictures could not be written to {failures_dir}: {error.strerror or error})"


def check_reference(spec: ReferenceSpec, answer: Any, base: Path, update: bool, failures_dir: Path, name: str) -> ReferenceCheck:
    """Compares the picture of a `screenshot` answer with `base / spec.png`.

    Not ok: the reference is missing or unreadable, has another size (never scaled silently), or more than `maxShare` of the compared pixels differ
    by more than `threshold`; the failure pictures are written to `failures_dir`. `update` makes the reference the picture instead (written when it
    is missing or differs at threshold 0, left alone when equal) and is always ok.
    """
    try:
        actual_png = base64.b64decode(answer[SCREENSHOT_PICTURE_KEY], validate=True)
        actual = read_png(actual_png)
    except (KeyError, TypeError, ValueError, binascii.Error, PngError) as error:
        return ReferenceCheck(False, "a picture in the answer", f"no usable picture: {error}")
    path = base / spec.png
    if update:
        return _update(path, actual_png, actual)

    wanted = f"{spec.png}: at most {_percent(spec.max_share)} of the pixels changed by more than {spec.threshold}"
    try:
        reference = read_png(path.read_bytes())
    except FileNotFoundError:
        return ReferenceCheck(False, wanted, f"the reference {path} does not exist; make it from this run with {UPDATE_HINT}")
    except (OSError, PngError) as error:
        return ReferenceCheck(False, wanted, f"the reference {path} cannot be read ({error}); make it again with {UPDATE_HINT}")
    if (reference.width, reference.height) != (actual.width, actual.height):
        saved = _write_failure_files(failures_dir, name, actual_png, None)
        return ReferenceCheck(False, wanted, f"size {_size(actual)}, the reference is {_size(reference)}; {saved}")
    try:
        found = png_diff.image_diff(reference, actual, spec.threshold, spec.region)
    except ValueError as error:
        return ReferenceCheck(False, wanted, f"the line cannot be compared: {error}")
    comparison = found.comparison
    if comparison.share <= spec.max_share:
        return ReferenceCheck(True)
    box = list(comparison.bbox) if comparison.bbox else None
    saved = _write_failure_files(failures_dir, name, actual_png, found.map)
    return ReferenceCheck(False, wanted, f"{_percent(comparison.share)} changed ({comparison.changed} of {comparison.total} px), box {box}; {saved}")


def _update(path: Path, actual_png: bytes, actual: PngImage) -> ReferenceCheck:
    """Makes `path` the game's picture when it is missing or differs at threshold 0; the bytes are the game's own."""
    verb, detail = "created", ""
    try:
        old = read_png(path.read_bytes())
    except FileNotFoundError:
        pass
    except (OSError, PngError):
        verb = "updated"
    else:
        verb = "updated"
        if (old.width, old.height) != (actual.width, actual.height):
            detail = f" (size {_size(old)} -> {_size(actual)})"
        else:
            found = png_diff.image_diff(old, actual)
            if found.comparison.changed == 0:
                return ReferenceCheck(True)
            detail = f" ({_percent(found.comparison.share)} changed)"
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(actual_png)
    except OSError as error:
        return ReferenceCheck(False, f"a writable reference {path}", f"cannot write it: {error.strerror or error}")
    return ReferenceCheck(True, note=f"{verb} reference {path}{detail}")
