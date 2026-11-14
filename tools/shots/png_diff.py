"""The comparison of two pictures for the screenshot tools (`tools/shots/CLAUDE.md`): the share of changed pixels, the boxes around them and a
difference map. Standard library only.

Pure: it reads decoded `PngImage`s and returns numbers, text and pictures, with no file, process or knowledge of any game. Rows that are
byte-equal are skipped without a pixel loop, so a large picture with a small change is cheap.
"""

import sys
from collections import deque
from collections.abc import Iterator, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import png_ops  # noqa: E402  (also puts the PNG reader on sys.path)
from png_reader import PngImage  # noqa: E402

CELL_SIZE = 8  # pixels: changed pixels are counted in cells of this size, and neighbouring cells form one region
MAX_REGIONS = 5  # regions reported per pair, the ones with the most changed pixels
PERCENT = 100.0
MAX_CHANNEL = 255
MAX_THRESHOLD = MAX_CHANNEL
RGB_CHANNELS = 3
MAP_DIM = 0.5  # the difference map shows A as grey at this fraction of its brightness, so the red of a change stands out
MAP_CHANGED = (255, 0, 0)
LUMA_RED, LUMA_GREEN, LUMA_BLUE = 0.299, 0.587, 0.114  # Rec. 601, the weights of a grey made from a color
ASPECT_TOLERANCE = 0.01  # the relative difference of two aspect ratios that is still "the same shape" (a rounded size is not a stretch)
BOX_CORNERS = 4

Box = tuple[int, int, int, int]  # x0, y0, x1, y1, corners inclusive


@dataclass
class Region:
    """A box in pixels, the corners inclusive, and the number of changed pixels inside it."""

    x0: int
    y0: int
    x1: int
    y1: int
    changed: int

    @property
    def area(self) -> int:
        return (self.x1 - self.x0 + 1) * (self.y1 - self.y0 + 1)

    def overlaps(self, other: "Region") -> bool:
        return self.x0 <= other.x1 and other.x0 <= self.x1 and self.y0 <= other.y1 and other.y0 <= self.y1

    def merged(self, other: "Region") -> "Region":
        return Region(min(self.x0, other.x0), min(self.y0, other.y0), max(self.x1, other.x1), max(self.y1, other.y1), self.changed + other.changed)


@dataclass
class Comparison:
    """Two pictures of one size: `changed` pixels of `total` (the compared area), `share` of them (0..1), `region_count` boxes found and the biggest `regions`."""

    changed: int
    total: int
    regions: list[Region] = field(default_factory=list)
    region_count: int = 0
    bbox: Box | None = None  # the exact box of every changed pixel, None when nothing changed

    @property
    def share(self) -> float:
        return self.changed / self.total if self.total else 0.0


@dataclass
class SizeChange:
    """Two pictures that cannot be compared: another size or another number of channels."""

    before: tuple[int, int, int]  # width, height, channels
    after: tuple[int, int, int]


def _size_change_text(change: SizeChange) -> str:
    if change.before[:2] != change.after[:2]:
        return f"size {change.before[0]}x{change.before[1]} -> {change.after[0]}x{change.after[1]}"
    return f"channels {change.before[2]} -> {change.after[2]}"


def check_region(width: int, height: int, region: Sequence[int] | None) -> Box | None:
    """The region as a box, or a ValueError that says what is wrong with it: four ints, corners inclusive, inside a picture of `width` x `height`."""
    if region is None:
        return None
    if len(region) != BOX_CORNERS or not all(isinstance(corner, int) and not isinstance(corner, bool) for corner in region):
        raise ValueError(f"the region must be four whole numbers [x0, y0, x1, y1], not {list(region)}")
    x0, y0, x1, y1 = region
    if x0 > x1 or y0 > y1:
        raise ValueError(f"the region [{x0},{y0},{x1},{y1}] has its corners the wrong way round: x0 <= x1 and y0 <= y1 are needed")
    if x0 < 0 or y0 < 0 or x1 >= width or y1 >= height:
        raise ValueError(f"the region [{x0},{y0},{x1},{y1}] is not inside the picture {width}x{height} (corners are inclusive pixels)")
    return x0, y0, x1, y1


def _changed_columns(row_before: bytes, row_after: bytes, channels: int, tolerance: int, x0: int, x1: int) -> Iterator[int]:
    """The x of each pixel of the columns `x0..x1` that has a channel differing by more than `tolerance`, once each, in order."""
    seen_x = -1  # the last pixel yielded: a pixel with several differing channels counts once
    first = x0 * channels
    window = zip(row_before[first : (x1 + 1) * channels], row_after[first : (x1 + 1) * channels])
    for byte, (a, b) in enumerate(window):
        if abs(a - b) <= tolerance:
            continue
        x = x0 + byte // channels
        if x != seen_x:
            seen_x = x
            yield x


def _changed_cells(before: PngImage, after: PngImage, tolerance: int, box: Box) -> dict[tuple[int, int], Region]:
    """The cells that hold changed pixels of the box, each as a `Region` of the pixels changed in it."""
    x0, y0, x1, y1 = box
    channels = before.channels
    cells: dict[tuple[int, int], Region] = {}
    for y in range(y0, y1 + 1):
        row_before, row_after = before.rows[y], after.rows[y]
        if row_before == row_after:
            continue
        for x in _changed_columns(row_before, row_after, channels, tolerance, x0, x1):
            key = (x // CELL_SIZE, y // CELL_SIZE)
            cell = cells.get(key)
            if cell is None:
                cells[key] = Region(x, y, x, y, 1)
            else:
                cell.x0 = min(cell.x0, x)
                cell.x1 = max(cell.x1, x)
                cell.y1 = y  # rows come in order
                cell.changed += 1
    return cells


def _group(cells: dict[tuple[int, int], Region]) -> list[Region]:
    """One box per group of 8-connected cells."""
    groups: list[Region] = []
    visited: set[tuple[int, int]] = set()
    for start in cells:
        if start in visited:
            continue
        visited.add(start)
        queue = deque([start])
        box = None
        while queue:
            cx, cy = queue.popleft()
            cell = cells[(cx, cy)]
            box = cell if box is None else box.merged(cell)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    neighbour = (cx + dx, cy + dy)
                    if neighbour in cells and neighbour not in visited:
                        visited.add(neighbour)
                        queue.append(neighbour)
        groups.append(Region(box.x0, box.y0, box.x1, box.y1, box.changed))
    return groups


def _merge_overlapping(boxes: list[Region]) -> list[Region]:
    boxes = list(boxes)
    merged_one = True
    while merged_one:
        merged_one = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                if boxes[i].overlaps(boxes[j]):
                    boxes[i] = boxes[i].merged(boxes[j])
                    del boxes[j]
                    merged_one = True
                    break
            if merged_one:
                break
    return boxes


def compare(before: PngImage, after: PngImage, tolerance: int = 0, region: Sequence[int] | None = None) -> Comparison | SizeChange:
    """A pixel is changed when any channel differs by more than `tolerance`. Another size or channel count is a `SizeChange`.

    `region` `[x0, y0, x1, y1]` (corners inclusive, in the pixels of the whole picture) limits the comparison to that box: `total` is its area and
    the boxes found keep the coordinates of the whole picture. A region that is not inside the picture is a ValueError.
    """
    sizes = ((before.width, before.height, before.channels), (after.width, after.height, after.channels))
    if sizes[0] != sizes[1]:
        return SizeChange(*sizes)
    box = check_region(before.width, before.height, region) or (0, 0, before.width - 1, before.height - 1)
    cells = _changed_cells(before, after, tolerance, box)
    changed = sum(cell.changed for cell in cells.values())
    boxes = _merge_overlapping(_group(cells))
    boxes.sort(key=lambda found: (-found.changed, found.y0, found.x0))
    bbox = None
    if cells:
        bbox = (min(c.x0 for c in cells.values()), min(c.y0 for c in cells.values()), max(c.x1 for c in cells.values()), max(c.y1 for c in cells.values()))
    return Comparison(changed, (box[2] - box[0] + 1) * (box[3] - box[1] + 1), boxes[:MAX_REGIONS], len(boxes), bbox)


def describe(name: str, result: Comparison | SizeChange) -> list[str]:
    """The text lines of one pair: the summary line, then one line per reported region."""
    if isinstance(result, SizeChange):
        return [f"{name}: {_size_change_text(result)}, not compared"]
    if result.changed == 0:
        return [f"{name}: unchanged"]
    noun = "region" if result.region_count == 1 else "regions"
    lines = [f"{name}: {result.share * PERCENT:.2f}% changed ({result.changed} of {result.total} px), {result.region_count} {noun}"]
    for box in result.regions:
        of_picture = box.area / result.total * PERCENT
        inside = box.changed / box.area * PERCENT
        lines.append(f"  [{box.x0},{box.y0},{box.x1},{box.y1}] {of_picture:.2f}% of the picture, {inside:.0f}% changed inside")
    return lines


def _grey_row(row: bytes, channels: int) -> bytes:
    """The luma of each pixel of a row, dimmed by `MAP_DIM`."""
    reds, greens, blues = row[0::channels], row[1::channels], row[2::channels]
    return bytes(int((LUMA_RED * r + LUMA_GREEN * g + LUMA_BLUE * b) * MAP_DIM) for r, g, b in zip(reds, greens, blues))


def diff_map(before: PngImage, after: PngImage, tolerance: int = 0, region: Sequence[int] | None = None) -> PngImage:
    """An RGB picture of the size of `before`: `before` dimmed to grey, the pixels changed by more than `tolerance` red, only inside the `region` when given.

    The two pictures must have one size and one channel count (`image_diff` makes them so). A region that is not inside them is a ValueError.
    """
    if (before.width, before.height, before.channels) != (after.width, after.height, after.channels):
        raise ValueError("a difference map needs two pictures of one size and one channel count")
    x0, y0, x1, y1 = check_region(before.width, before.height, region) or (0, 0, before.width - 1, before.height - 1)
    rows = []
    for y, (row_before, row_after) in enumerate(zip(before.rows, after.rows)):
        grey = _grey_row(row_before, before.channels)
        pixels = bytearray(before.width * RGB_CHANNELS)
        for channel in range(RGB_CHANNELS):
            pixels[channel::RGB_CHANNELS] = grey
        if y0 <= y <= y1 and row_before != row_after:
            for x in _changed_columns(row_before, row_after, before.channels, tolerance, x0, x1):
                pixels[x * RGB_CHANNELS : (x + 1) * RGB_CHANNELS] = bytes(MAP_CHANGED)
        rows.append(bytes(pixels))
    return PngImage(before.width, before.height, RGB_CHANNELS, rows)


@dataclass
class ImageDiff:
    """The whole comparison of picture A with picture B: the numbers, the difference map and what was done to B to make it comparable."""

    comparison: Comparison
    map: PngImage
    threshold: int
    region: Box | None
    a_info: tuple[int, int, int]  # width, height, channels as given
    b_info: tuple[int, int, int]
    scaled: bool  # B had another size and was scaled to A's
    aspect_differs: bool  # ... and its shape was not A's, so the picture was stretched

    def stats(self) -> dict[str, Any]:
        """The numbers as a JSON-ready dict: `share` is of the compared area (the region when given), `shareOfPicture` of a box is of the whole picture A."""
        found = self.comparison
        picture_area = self.a_info[0] * self.a_info[1]
        return {
            "equal": found.changed == 0,
            "changed": found.changed,
            "compared": found.total,
            "share": found.share,
            "boundingBox": list(found.bbox) if found.bbox else None,
            "regions": [{"box": [r.x0, r.y0, r.x1, r.y1], "changed": r.changed, "shareOfPicture": r.area / picture_area} for r in found.regions],
            "threshold": self.threshold,
            "region": list(self.region) if self.region else None,
            "a": {"width": self.a_info[0], "height": self.a_info[1], "channels": self.a_info[2]},
            "b": {"width": self.b_info[0], "height": self.b_info[1], "channels": self.b_info[2]},
            "scaled": self.scaled,
            "aspectDiffers": self.aspect_differs,
        }


def image_diff(a: PngImage, b: PngImage, threshold: int = 0, region: Sequence[int] | None = None) -> ImageDiff:
    """Compares `b` with `a` (the reference side). A 4 channel picture beside a 3 channel one loses its alpha, a `b` of another size is scaled to `a`'s.

    A bad `threshold` (0..255) or a `region` outside `a` is a ValueError with the reason.
    """
    if isinstance(threshold, bool) or not isinstance(threshold, int) or not 0 <= threshold <= MAX_THRESHOLD:
        raise ValueError(f"the threshold must be a whole number from 0 to {MAX_THRESHOLD}, not {threshold!r}")
    box = check_region(a.width, a.height, region)
    a_info = (a.width, a.height, a.channels)
    b_info = (b.width, b.height, b.channels)
    if a.channels != b.channels:
        a, b = png_ops.to_channels(a, RGB_CHANNELS), png_ops.to_channels(b, RGB_CHANNELS)
    scaled = (b.width, b.height) != (a.width, a.height)
    aspect_differs = scaled and abs(b.width / b.height / (a.width / a.height) - 1.0) > ASPECT_TOLERANCE
    if scaled:
        b = png_ops.scale_to(b, a.width, a.height)
    comparison = compare(a, b, threshold, box)
    assert isinstance(comparison, Comparison)  # one size and one channel count by now
    return ImageDiff(comparison, diff_map(a, b, threshold, box), threshold, box, a_info, b_info, scaled, aspect_differs)
