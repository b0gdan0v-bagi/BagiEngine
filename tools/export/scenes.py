"""Leaves scenes out of a copy of a game's content that an export ships. Standard library only; the rules: tools/export/CLAUDE.md.

The file is read with patterns, not `xml.etree`: its parser needs pyexpat, which a Homebrew Python can lack, and it refuses a
`--` inside a comment, which the engine's reader accepts. The elements left out are cut from the bytes, so the rest stays as it was.
"""

from __future__ import annotations

import html
import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

XML_NAME = "SceneConfig.xml"
BINARY_NAME = "SceneConfig.bin"
SCENES_TAG = "scenes"
NAME_ATTRIBUTE = "name"

_MARKUP = re.compile(
    rb"""<!--.*?-->
       | <!\[CDATA\[.*?\]\]>
       | <\?.*?\?>
       | <!DOCTYPE[^<>]*>
       | <(?P<close>/)?(?P<tag>[^\s/<>!?"'=]+)(?P<attributes>(?:"[^"]*"|'[^']*'|[^"'<>])*)>
       | (?P<broken><)
    """, re.DOTALL | re.VERBOSE)
_ATTRIBUTE = re.compile(rb"""(?P<key>[^\s=/"']+)\s*=\s*(?:"(?P<double>[^"]*)"|'(?P<single>[^']*)')""")


class SceneError(Exception):
    """The scenes cannot be left out; the message says why."""


@dataclass(frozen=True)
class _Scene:
    name: str
    start: int  # the bytes of the whole element
    end: int


def _name_of(attributes: bytes) -> str:
    for match in _ATTRIBUTE.finditer(attributes):
        if match["key"].decode("utf-8", "replace") == NAME_ATTRIBUTE:
            value = match["double"] if match["double"] is not None else match["single"]
            return html.unescape(value.decode("utf-8", "replace"))
    return ""


def _read_scenes(data: bytes) -> list[_Scene]:
    """The child elements of the root's first `<scenes>`, in file order."""

    def broken(offset: int) -> SceneError:
        line = data.count(b"\n", 0, offset) + 1
        return SceneError(f"{XML_NAME} does not parse (line {line})")

    scenes: list[_Scene] = []
    open_tags: list[bytes] = []
    roots = 0
    list_depth = None  # the depth of the children of <scenes> while it is open
    list_seen = False
    start, name = 0, ""
    for match in _MARKUP.finditer(data):
        if match["broken"]:
            raise broken(match.start())
        tag = match["tag"]
        if tag is None:
            continue
        if match["close"]:
            if not open_tags or open_tags.pop() != tag:
                raise broken(match.start())
        else:
            depth = len(open_tags)
            if depth == 0:
                roots += 1
            if depth == list_depth:
                start, name = match.start(), _name_of(match["attributes"])
            elif depth == 1 and not list_seen and tag == SCENES_TAG.encode():
                list_seen = True
                list_depth = None if match["attributes"].endswith(b"/") else depth + 1
            if not match["attributes"].endswith(b"/"):
                open_tags.append(tag)
                continue
        # An element ended here
        if len(open_tags) == list_depth:
            scenes.append(_Scene(name, start, match.end()))
        elif list_depth is not None and len(open_tags) < list_depth:
            list_depth = None
    if open_tags or roots != 1:
        raise broken(len(data))
    if not list_seen:
        raise SceneError(f"{XML_NAME} has no <{SCENES_TAG}>: there is no scene to leave out")
    return scenes


def _find_config(config_folders: Sequence[Path]) -> Path:
    for folder in reversed(config_folders):
        if (folder / XML_NAME).is_file():
            return folder / XML_NAME
        if (folder / BINARY_NAME).is_file():
            raise SceneError(f"the game reads the cooked {BINARY_NAME}: scenes can be left out only of an {XML_NAME}")
    raise SceneError(f"no content root has config/{XML_NAME}: there is no scene to leave out")


def exclude_scenes(config_folders: Sequence[Path], names: Sequence[str]) -> None:
    """Removes the scenes `names` from the `SceneConfig.xml` the game reads: that of the highest of `config_folders`, which are
    the `config/` folders of the layers, lowest first (one may be missing). Rewrites that file, so give it a copy of the content.
    SceneError, and nothing written, when a scene cannot be left out. Its message names no path: the copy is gone by the time
    it is read."""
    file = _find_config(config_folders)
    data = file.read_bytes()
    scenes = _read_scenes(data)
    present = [scene.name for scene in scenes]
    for name in names:
        if name not in present:
            raise SceneError(f"there is no scene '{name}' in {XML_NAME}; its scenes are {', '.join(present) or 'none'}")
        if name == present[0]:
            raise SceneError(f"the scene '{name}' is the first of {XML_NAME} and cannot be left out: the game opens on it")
    for scene in reversed(scenes):
        if scene.name in names:
            data = data[:scene.start] + data[scene.end:]
    file.write_bytes(data)
