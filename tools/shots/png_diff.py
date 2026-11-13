"""The comparison of two pictures for the screenshot tool (`tools/shots/CLAUDE.md`): the share of changed pixels and the boxes around them. Standard library only.

Pure: it reads two decoded `PngImage`s and returns numbers and text, with no file, process or knowledge of any game. Rows that are
byte-equal are skipped without a pixel loop, so a large picture with a small change is cheap.
"""

import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

# The PNG reader lives with the control tools (the game's `ui_shots.py` imports it from there too): one place, so it is not copied.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control" / "tests"))

from png_reader import PngImage  # noqa: E402

CELL_SIZE = 8  # pixels: changed pixels are counted in cells of this size, and neighbouring cells form one region
MAX_REGIONS = 5  # regions reported per pair, the ones with the most changed pixels
PERCENT = 100.0


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
    """Two pictures of one size: `changed` pixels of `total`, `share` of them (0..1), `region_count` boxes found and the biggest `regions`."""

    changed: int
    total: int
    regions: list[Region] = field(default_factory=list)
    region_count: int = 0

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


def _changed_cells(before: PngImage, after: PngImage, tolerance: int) -> dict[tuple[int, int], Region]:
    """The cells that hold changed pixels, each as a `Region` of the pixels changed in it."""
    channels = before.channels
    cells: dict[tuple[int, int], Region] = {}
    for y, (row_before, row_after) in enumerate(zip(before.rows, after.rows)):
        if row_before == row_after:
            continue
        seen_x = -1  # the last pixel counted in this row: a pixel with several differing channels counts once
        for byte, (a, b) in enumerate(zip(row_before, row_after)):
            if abs(a - b) <= tolerance:
                continue
            x = byte // channels
            if x == seen_x:
                continue
            seen_x = x
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


def compare(before: PngImage, after: PngImage, tolerance: int = 0) -> Comparison | SizeChange:
    """A pixel is changed when any channel differs by more than `tolerance`. Another size or channel count is a `SizeChange`."""
    sizes = ((before.width, before.height, before.channels), (after.width, after.height, after.channels))
    if sizes[0] != sizes[1]:
        return SizeChange(*sizes)
    cells = _changed_cells(before, after, tolerance)
    changed = sum(cell.changed for cell in cells.values())
    boxes = _merge_overlapping(_group(cells))
    boxes.sort(key=lambda box: (-box.changed, box.y0, box.x0))
    return Comparison(changed, before.width * before.height, boxes[:MAX_REGIONS], len(boxes))


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
