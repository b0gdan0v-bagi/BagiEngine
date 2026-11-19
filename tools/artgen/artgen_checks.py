"""
The checks of a cut-out sprite (tools/artgen/CLAUDE.md, "The service"): from the alpha channel of a variant they find what a
person sees at a glance: nothing drawn, an object cut by the frame, several pieces, a wide soft halo, a background that was
not removed, a dot, an object far from the centre, several objects that touch. A verdict is plain data for the provenance file and the progress event.

The object is every pixel with alpha at or above `cut`. The shape checks run on a mask reduced to at most `MAX_SIDE` pixels on
its longer side (a reduced pixel is set when any pixel of its cell is), so a 1024 picture costs well under a second. Standard
library only.
"""

import math

import artgen_alpha

# What each threshold means; a preset's `checks` replaces any of them by name (`artgen_preset.parse_preset`).
DEFAULTS = {
    "cut": 128,  # the alpha (1 to 255) from which a pixel belongs to the object
    "maxEdgeRun": 0.01,  # the longest stretch of one frame edge the object may run along, a fraction of that side
    "maxPieces": 1,  # the number of separate pieces allowed (their gap is a full valley: with more than one, raise maxValley too)
    "minPieceArea": 0.01,  # a piece counts when it holds at least this fraction of the object's area (specks do not)
    "maxValley": 0.7,  # how deep a valley between two bodies may go, as a fraction of the lower one's height (1: no limit)
    "minBodyWidth": 0.04,  # a part narrower than this fraction of the object's width is no body (a crown's tip, an antenna)
    "maxHalo": 0.10,  # the share of semi-transparent pixels among the non-transparent ones
    "maxFill": 0.85,  # the share of the frame the object may cover before the background counts as not removed
    "minCover": 0.10,  # the share of the frame's area the object's bounding box must cover
    "maxOffset": 0.25,  # how far the bounding box's centre may be from the frame's centre, as a fraction of the side
}
# The accepted values of each threshold: (type, smallest, largest); a fraction is a number from 0 to 1
FRACTION = (float, 0, 1)
RANGES = {
    "cut": (int, 1, 255),
    "maxEdgeRun": FRACTION,
    "maxPieces": (int, 1, None),
    "minPieceArea": FRACTION,
    "maxValley": FRACTION,
    "minBodyWidth": FRACTION,
    "maxHalo": FRACTION,
    "maxFill": FRACTION,
    "minCover": FRACTION,
    "maxOffset": FRACTION,
}
CHECK_NAMES = ("empty", "edge", "pieces", "bodies", "halo", "background", "small", "offCentre")

MAX_SIDE = 256
TRANSPARENT_BELOW = 8  # alpha under this is transparent for the halo's count
SEMI_ABOVE = 247  # alpha above this is opaque for the halo's count and for the corners
METRIC_DIGITS = 4
NON_TRANSPARENT_TABLE = bytes(1 if value >= TRANSPARENT_BELOW else 0 for value in range(256))
SEMI_TABLE = bytes(1 if TRANSPARENT_BELOW <= value <= SEMI_ABOVE else 0 for value in range(256))
NEIGHBOURS = ((1, 0), (-1, 0), (0, 1), (0, -1))


def _or_all(parts):
    """The bytewise OR of equally long 0/1 byte strings."""
    value = 0
    for part in parts:
        value |= int.from_bytes(part, "big")
    return value.to_bytes(len(parts[0]), "big")


def _reduce(mask, width, height):
    """The mask at most MAX_SIDE on its longer side, with its size: a reduced pixel is set when any pixel of its cell is."""
    scale = -(-max(width, height) // MAX_SIDE)
    if scale == 1:
        return mask, width, height
    reduced_width = -(-width // scale)
    reduced_height = -(-height // scale)
    padding = bytes(reduced_width * scale - width)
    rows = []
    for y in range(height):
        row = mask[y * width:(y + 1) * width] + padding
        rows.append(_or_all([row[offset::scale] for offset in range(scale)]))
    reduced = b"".join(_or_all(rows[y:y + scale]) for y in range(0, height, scale))
    return reduced, reduced_width, reduced_height


def _longest_run(line):
    return max(len(part) for part in line.split(b"\x00"))


def _edge_run(mask, width, height):
    """The longest stretch of the object along one frame edge, as a fraction of that edge's length."""
    horizontal = max(_longest_run(mask[:width]), _longest_run(mask[(height - 1) * width:])) / width
    vertical = max(_longest_run(mask[0::width]), _longest_run(mask[width - 1::width])) / height
    return max(horizontal, vertical)


def _pieces(mask, width, height, min_area):
    """How many 4-connected pieces of the mask hold at least `min_area` pixels (an iterative fill over a zero border)."""
    stride = width + 2
    grid = bytearray(stride * (height + 2))
    for y in range(height):
        grid[(y + 1) * stride + 1:(y + 1) * stride + 1 + width] = mask[y * width:(y + 1) * width]
    steps = [dx + dy * stride for dx, dy in NEIGHBOURS]
    count = 0
    for start in range(len(grid)):
        if not grid[start]:
            continue
        grid[start] = 0
        stack = [start]
        area = 0
        while stack:
            cell = stack.pop()
            area += 1
            for step in steps:
                if grid[cell + step]:
                    grid[cell + step] = 0
                    stack.append(cell + step)
        if area >= min_area:
            count += 1
    return count


def _column_heights(mask, width, height):
    """Per column of the mask's bounding box, from its first set pixel down to the box's bottom (a hole inside counts as
    filled: a handle's opening is closed, a valley between two bodies is open to the top; an empty column is 0)."""
    tops = [None] * width
    unresolved = width
    bottom = 0
    for y in range(height):
        row = mask[y * width:(y + 1) * width]
        if not any(row):
            continue
        bottom = y + 1
        if unresolved:
            for x in range(width):
                if row[x] and tops[x] is None:
                    tops[x] = y
                    unresolved -= 1
    columns = [x for x, top in enumerate(tops) if top is not None]
    return [0 if top is None else bottom - top for top in tops[columns[0]:columns[-1] + 1]]


def _opened(profile, window):
    """The profile after a min filter and a max filter of `window` columns: thin parts and spikes are gone, wide ones stay."""
    count = len(profile)
    window = min(window, count)
    eroded = [min(profile[i:i + window]) for i in range(count - window + 1)]
    return [max(eroded[max(0, j - window + 1):j + 1]) for j in range(count)]


def _valley(mask, width, height, min_body_width):
    """How deep the deepest valley between two bodies of the silhouette goes: its depth over the lower of the two peaks
    around it (water-fill over the opened column heights), 0 for one body, 1 when it reaches the floor."""
    heights = _column_heights(mask, width, height)
    profile = _opened(heights, max(1, round(min_body_width * len(heights))))
    count = len(profile)
    left_peak = []
    peak = 0
    for value in profile:
        peak = max(peak, value)
        left_peak.append(peak)
    right_peak = [0] * count
    peak = 0
    for i in range(count - 1, -1, -1):
        peak = max(peak, profile[i])
        right_peak[i] = peak
    valley = 0.0
    for i in range(count):
        lower = min(left_peak[i], right_peak[i])
        if lower > 0:
            valley = max(valley, (lower - profile[i]) / lower)
    return valley


def _bounding_box(mask, width, height):
    """(left, top, right, bottom) of the set pixels as fractions of the frame, right and bottom exclusive."""
    rows = [mask[y * width:(y + 1) * width] for y in range(height)]
    used_rows = [y for y, row in enumerate(rows) if any(row)]
    used_columns = _or_all(rows)
    columns = [x for x, value in enumerate(used_columns) if value]
    return columns[0] / width, used_rows[0] / height, (columns[-1] + 1) / width, (used_rows[-1] + 1) / height


def _rounded(value):
    return round(value, METRIC_DIGITS)


def merge_thresholds(given):
    """`DEFAULTS` with the thresholds of the object `given` replaced; a ValueError says which name or value is wrong."""
    if not isinstance(given, dict):
        raise ValueError("must be false or an object of thresholds")
    for name, value in given.items():
        if name not in DEFAULTS:
            raise ValueError(f"has no threshold '{name}' (it has: {', '.join(DEFAULTS)})")
        kind, smallest, largest = RANGES[name]
        numeric = type(value) is int if kind is int else type(value) in (int, float)
        if not numeric or value < smallest or (largest is not None and value > largest):
            bound = f"from {smallest} to {largest}" if largest is not None else f"{smallest} or more"
            raise ValueError(f"threshold '{name}' must be {'an integer' if kind is int else 'a number'} {bound}")
    return {**DEFAULTS, **given}


def check_alpha(alpha, thresholds=None):
    """The verdict {"passed", "failed", "metrics", "thresholds"} of an `Alpha`; `thresholds` names the ones to change from
    `DEFAULTS`."""
    used = {**DEFAULTS, **(thresholds or {})}
    width, height, pixels = alpha.width, alpha.height, alpha.pixels
    cut_table = bytes(1 if value >= used["cut"] else 0 for value in range(256))
    mask = pixels.translate(cut_table)
    area = mask.count(1)
    failed = []
    metrics = {"fill": 0.0}
    if area == 0:
        failed.append("empty")
    else:
        reduced, reduced_width, reduced_height = _reduce(mask, width, height)
        left, top, right, bottom = _bounding_box(reduced, reduced_width, reduced_height)
        reduced_area = reduced.count(1)
        pieces = _pieces(reduced, reduced_width, reduced_height, used["minPieceArea"] * reduced_area)
        non_transparent = pixels.translate(NON_TRANSPARENT_TABLE).count(1)
        semi = pixels.translate(SEMI_TABLE).count(1)
        corners = (pixels[0], pixels[width - 1], pixels[(height - 1) * width], pixels[-1])
        metrics = {
            "fill": area / len(pixels),
            "edgeRun": _edge_run(mask, width, height),
            "pieces": pieces,
            "valley": _valley(reduced, reduced_width, reduced_height, used["minBodyWidth"]),
            "halo": semi / non_transparent if non_transparent else 0.0,
            "cover": (right - left) * (bottom - top),
            "offset": math.hypot((left + right) / 2 - 0.5, (top + bottom) / 2 - 0.5),
        }
        if metrics["edgeRun"] > used["maxEdgeRun"]:
            failed.append("edge")
        if pieces > used["maxPieces"]:
            failed.append("pieces")
        elif metrics["valley"] > used["maxValley"]:
            failed.append("bodies")
        if metrics["halo"] > used["maxHalo"]:
            failed.append("halo")
        if metrics["fill"] > used["maxFill"] or all(corner > SEMI_ABOVE for corner in corners):
            failed.append("background")
        if metrics["cover"] < used["minCover"]:
            failed.append("small")
        if metrics["offset"] > used["maxOffset"]:
            failed.append("offCentre")
    return {
        "passed": not failed,
        "failed": failed,
        "metrics": {name: _rounded(value) if isinstance(value, float) else value for name, value in metrics.items()},
        "thresholds": used,
    }


def check_png(data, thresholds=None):
    """The verdict for the bytes of a PNG; None when it has no alpha channel (an opaque picture: the checks do not apply). A
    picture that cannot be read gives {"passed": None, "error": reason}; nothing is raised."""
    try:
        alpha = artgen_alpha.read_alpha(data)
    except artgen_alpha.AlphaError as error:
        return {"passed": None, "error": str(error)}
    if alpha is None:
        return None
    return check_alpha(alpha, thresholds)
