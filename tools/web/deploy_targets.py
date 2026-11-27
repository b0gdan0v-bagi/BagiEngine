"""The named deploy targets of a project: `<project>/web/targets.json`, read by `build_and_deploy.py --target NAME`.

    {"format": 1, "targets": {
        "mahjong-editor": {"game": "BagiMahjong", "preset": "web-editor", "service": "studio-mahjong", "address": "studio-mahjong.bagiz.dev"},
        "engine-demo": {"game": "BagiDemo", "preset": "web-editor", "site": "engine.bagiz.dev", "page": "demo"}}}

Two kinds. A Railway service: `service` (the Railway service) and `address` (the host name it answers on). A page of a site: `site` (the
host name of the site that takes the build) and `page` (the name of the page it switches). A target has the pair of one kind and no key of
the other. Required in both: `game` (the CMake target), `preset` (a web preset). Optional: `base` (the branch the checkout is cut from,
default main), `area` (the todo area `alltests.py --area` gets, default engine). Unknown keys are refused so that a typo cannot pass. Every
value ends up as one argument of a command or a part of a URL, never in a shell, and none can start with `-`, so the patterns below are
strict on purpose. Standard library only.
"""

import json
import re
from pathlib import Path
from typing import NamedTuple

TARGETS_FILE = Path("web") / "targets.json"
FORMAT_VERSION = 1
# A file this large is not a list of targets; refusing it keeps a wrong path (a log, a binary) from being parsed at all
MAX_FILE_BYTES = 64 * 1024
DEFAULT_BASE = "main"
DEFAULT_AREA = "engine"

NAME_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
GAME_PATTERN = re.compile(r"[A-Za-z0-9_]+")
PRESET_PATTERN = re.compile(r"web-[a-z0-9-]+")
SERVICE_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
ADDRESS_PATTERN = re.compile(r"[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*")
SITE_PATTERN = ADDRESS_PATTERN
PAGE_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
# A branch name as git allows it, narrowed: no leading `-` or `/`, no `..`, no spaces or glob characters
BASE_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/-]*")
AREA_PATTERN = re.compile(r"[a-z][a-z0-9-]*")
REQUIRED_KEYS = ("game", "preset")
SERVICE_KEYS = ("service", "address")
SITE_KEYS = ("site", "page")
OPTIONAL_KEYS = ("base", "area")
PATTERNS = {
    "game": GAME_PATTERN, "preset": PRESET_PATTERN, "service": SERVICE_PATTERN, "address": ADDRESS_PATTERN,
    "site": SITE_PATTERN, "page": PAGE_PATTERN, "base": BASE_PATTERN, "area": AREA_PATTERN,
}
KIND_SERVICE = "service"
KIND_SITE = "site"


class TargetsError(Exception):
    """The targets file is missing or wrong; the message names the file and the key."""


class Target(NamedTuple):
    name: str
    game: str
    preset: str
    service: str | None = None
    address: str | None = None
    base: str = DEFAULT_BASE
    area: str = DEFAULT_AREA
    site: str | None = None
    page: str | None = None

    @property
    def kind(self) -> str:
        """`service` (a Railway service) or `site` (a page of a site); `_parse_target` guarantees the pair of exactly one."""
        return KIND_SITE if self.site is not None else KIND_SERVICE


def _object_without_duplicates(pairs: list[tuple[str, object]]) -> dict:
    result: dict = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"the key {key!r} appears twice in one object")
        result[key] = value
    return result


def _read(path: Path) -> object:
    try:
        size = path.stat().st_size
    except OSError as error:
        raise TargetsError(f"{path}: cannot read the targets file: {error.strerror or error}") from error
    if size > MAX_FILE_BYTES:
        raise TargetsError(f"{path}: {size} bytes is more than the {MAX_FILE_BYTES} a targets file may have")
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_object_without_duplicates)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise TargetsError(f"{path}: not valid JSON in UTF-8: {error}") from error


def _check_unknown_keys(path: Path, where: str, entries: dict, known: tuple[str, ...]) -> None:
    unknown = sorted(set(entries) - set(known))
    if unknown:
        raise TargetsError(f"{path}: {where}: unknown key {unknown[0]!r} (known: {', '.join(known)})")


def _kind_keys(path: Path, where: str, entry: dict) -> tuple[str, ...]:
    """The pair of keys the target's kind needs; refuses keys of both kinds and none of either."""
    service_given = [key for key in SERVICE_KEYS if key in entry]
    site_given = [key for key in SITE_KEYS if key in entry]
    if service_given and site_given:
        raise TargetsError(f"{path}: {where}: the keys {', '.join(map(repr, service_given))} (a Railway service) and "
                           f"{', '.join(map(repr, site_given))} (a page of a site) cannot be in one target")
    if service_given:
        return SERVICE_KEYS
    if site_given:
        return SITE_KEYS
    raise TargetsError(f"{path}: {where}: needs {' and '.join(map(repr, SERVICE_KEYS))} (a Railway service) "
                       f"or {' and '.join(map(repr, SITE_KEYS))} (a page of a site)")


def _parse_target(path: Path, name: str, entry: object) -> Target:
    where = f"target {name!r}"
    if not isinstance(entry, dict):
        raise TargetsError(f"{path}: {where}: must be an object")
    _check_unknown_keys(path, where, entry, REQUIRED_KEYS + SERVICE_KEYS + SITE_KEYS + OPTIONAL_KEYS)
    for key in REQUIRED_KEYS:
        if key not in entry:
            raise TargetsError(f"{path}: {where}: the key {key!r} is missing")
    pair = _kind_keys(path, where, entry)
    for key in pair:
        if key not in entry:
            raise TargetsError(f"{path}: {where}: the key {key!r} is missing (a target with {pair[0]!r} or {pair[1]!r} needs both)")
    values = {}
    for key, value in entry.items():
        pattern = PATTERNS[key]
        if not isinstance(value, str) or not pattern.fullmatch(value):
            raise TargetsError(f"{path}: {where}: {key!r} must match {pattern.pattern}, not {value!r}")
        if key == "base" and ".." in value:
            raise TargetsError(f"{path}: {where}: 'base' must not contain '..': {value!r}")
        values[key] = value
    return Target(name=name, **values)


def load(project: Path) -> dict[str, Target]:
    """The targets of `project`, by name. Raises TargetsError for a missing or wrong file."""
    path = project / TARGETS_FILE
    document = _read(path)
    if not isinstance(document, dict):
        raise TargetsError(f"{path}: the top level must be an object")
    _check_unknown_keys(path, "the top level", document, ("format", "targets"))
    version = document.get("format")
    if isinstance(version, bool) or version != FORMAT_VERSION:
        raise TargetsError(f"{path}: 'format' must be {FORMAT_VERSION}, not {version!r}")
    entries = document.get("targets")
    if not isinstance(entries, dict) or not entries:
        raise TargetsError(f"{path}: 'targets' must be an object with at least one target")
    targets = {}
    for name, entry in entries.items():
        if not NAME_PATTERN.fullmatch(name):
            raise TargetsError(f"{path}: target name {name!r} must match {NAME_PATTERN.pattern}")
        targets[name] = _parse_target(path, name, entry)
    return targets


def find(project: Path, name: str) -> Target:
    """The target `name` of `project`; an unknown name lists the known ones."""
    targets = load(project)
    if name not in targets:
        raise TargetsError(f"unknown target {name!r} in {project / TARGETS_FILE} (known: {', '.join(sorted(targets))})")
    return targets[name]
