"""Delta debugging over a list: the shortest sublist that still makes a predicate say "fails". Pure functions, standard library only.

    shortest = shrink(steps, lambda candidate: still_fails(candidate))

Used by bagi_play.py --shrink to cut a failing scenario down to the calls the failure needs (tools/control/CLAUDE.md, "Shrinking").
"""

import math
from typing import Callable, Sequence, TypeVar

T = TypeVar("T")

INITIAL_PARTS = 2  # the list is first cut into this many chunks, and chunks are dropped one at a time


def shrink(items: Sequence[T], fails: Callable[[list[T]], bool], max_probes: int | None = None) -> list[T]:
    """A 1-minimal sublist of `items` (order kept) for which `fails` is still true: no single item can be dropped from it.

    `fails` gets a fresh list every time and has to be deterministic. ValueError when `fails(items)` is false: there is nothing to shrink.
    The call that checks `items` counts as a probe; when `max_probes` probes have been made the best list so far is returned
    (then it is not necessarily 1-minimal). `items` is not changed.
    """
    probes = 0

    def probe(candidate: list[T]) -> bool:
        nonlocal probes
        probes += 1
        return fails(list(candidate))

    def spent() -> bool:
        return max_probes is not None and probes >= max_probes

    current = list(items)
    if not probe(current):
        raise ValueError("the list does not fail, so there is nothing to shrink")
    parts = INITIAL_PARTS
    while current and not spent():
        parts = min(parts, len(current))
        size = math.ceil(len(current) / parts)
        for start in range(0, len(current), size):
            if spent():
                return current
            candidate = current[:start] + current[start + size :]
            if probe(candidate):
                current = candidate
                parts = max(parts - 1, INITIAL_PARTS)  # the rest of the list is cut into fewer, bigger chunks than before
                break
        else:
            if parts >= len(current):
                break  # every single item was tried: 1-minimal
            parts = min(len(current), parts * 2)
    return current
