"""The scene file the editor's Save writes: what a saved `SceneConfig.xml` may differ in, and the guard that puts the content files back after a session.

Pure files and bytes, no process; standard library only (the work-tree check is tools/shots' `png_ops`).

    saved = compare_scene_files(before, after, "StressSprites", [{"kind": "attribute", "path": "Field", "name": "layer", "value": "2"}], ignore_children_of={"Field"})
    guard = ContentGuard([config_file], backup_dir)
    guard.arm()            # before the editor starts
    restored = guard.release()   # after it quit: the files it wrote are back, `restored` names them

`compare_scene_files` answers `Violation`s, all named `editor.*`:

- `editor.saved_file_unreadable`: the saved file is missing, is not well-formed XML or holds no scene.
- `editor.saved_edit_missing`: an edit that was asked for is not in the saved scene.
- `editor.saved_scene_changed`: the edited scene differs from the one before in anything but the edits (a node, an attribute, a component, the order); the
  children of the nodes named in `ignore_children_of` are skipped, since a game's component may build children at run time and Save writes the live tree.
- `editor.saved_other_scene_changed`: another scene differs, moved, vanished or is new, or the scenes are in another order.
- `editor.saved_root_changed`: the root element, its attributes (the content version) or what it holds besides the scenes differ.

An edit is a dict with `kind` and `path` (the node's names from the scene down, joined by `/`; a second sibling of one name is `name[2]`):
`attribute` (`name`, `value`: an attribute of the node), `component-attribute` (`component`, a type, `name`, `value`), `node-name` (`new_name`) and
`node-added` (the node with its subtree is taken from the saved file; `path` is the new node). Edits apply in order and a path follows the earlier
renames of the list. A path that names nothing in the file before is a `SceneFileError`: that is a bug of the request, not of the editor.
Tolerances are strict: an attribute the file before lacks and Save adds is a change, unless `added_defaults` (component type to `{attribute: value}`, the
game's allow list) names it with that value: the writer adds the defaults of a component it saves.

`ContentGuard` promises: `arm()` copies the bytes of each existing file into `backup_dir` (never inside a git work tree) before anything can write,
`release()` writes the bytes back where the file differs (removing a file that did not exist), checks the result byte by byte and answers the files it had to
restore. Idempotent; it never runs `git`.
"""

import copy
import sys
import xml.etree.ElementTree as ElementTree
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "shots"))
sys.path.insert(0, str(QA_DIR))

import png_ops  # noqa: E402
from bagi_qa_checks import Violation  # noqa: E402

NAME_FILE_UNREADABLE = "editor.saved_file_unreadable"
NAME_EDIT_MISSING = "editor.saved_edit_missing"
NAME_SCENE_CHANGED = "editor.saved_scene_changed"
NAME_OTHER_SCENE_CHANGED = "editor.saved_other_scene_changed"
NAME_ROOT_CHANGED = "editor.saved_root_changed"

EDIT_ATTRIBUTE = "attribute"
EDIT_COMPONENT_ATTRIBUTE = "component-attribute"
EDIT_NODE_NAME = "node-name"
EDIT_NODE_ADDED = "node-added"

SCENES_TAG = "scenes"
SCENE_TAG = "scene"
NODES_TAG = "nodes"
NODE_TAG = "node"
CHILDREN_TAG = "children"
COMPONENTS_TAG = "components"
COMPONENT_TAG = "component"
WRAPPER_TAGS = frozenset({NODES_TAG, CHILDREN_TAG, COMPONENTS_TAG})  # their children are named by their own names: the wrapper is not in a path
NAME_ATTRIBUTE = "name"
TYPE_ATTRIBUTE = "type"
REF_ATTRIBUTE = "ref"
PATH_SEPARATOR = "/"
ABSENT = "absent"
MAX_DIFFERENCES = 20  # reported per scene; the rest is counted in the last message

BACKUP_FILE = "{index}-{name}"


class SceneFileError(Exception):
    """A scene file that cannot be read, or an edit that names something the file before does not have; the text says which."""


class ContentGuardError(Exception):
    """A backup folder that is not allowed, a guard used twice, or a restore that did not leave the bytes as they were."""


@dataclass
class SceneFile:
    """A parsed `SceneConfig.xml` (comments dropped): the root element and its `<scene>` elements in file order."""

    path: str
    root: ElementTree.Element
    scenes: list[ElementTree.Element]

    @property
    def root_attributes(self) -> dict[str, str]:
        return dict(self.root.attrib)

    @property
    def scene_names(self) -> list[str]:
        return [scene.get(NAME_ATTRIBUTE, "") for scene in self.scenes]

    def scene(self, name: str) -> ElementTree.Element | None:
        return next((scene for scene in self.scenes if scene.get(NAME_ATTRIBUTE) == name), None)


def read_scene_file(path: str | Path) -> SceneFile:
    """Parses a scene file. SceneFileError when it is missing, not text or not well-formed XML."""
    file = Path(path)
    try:
        data = file.read_bytes()
    except OSError as error:
        raise SceneFileError(f"cannot read {file}: {error}") from error
    return parse_scene_file(data, str(file))


def parse_scene_file(data: bytes, origin: str = "<bytes>") -> SceneFile:
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise SceneFileError(f"{origin} is not well-formed XML: {error}") from error
    scenes_element = root.find(SCENES_TAG)
    scenes = list(scenes_element.findall(SCENE_TAG)) if scenes_element is not None else []
    return SceneFile(origin, root, scenes)


def compare_scene_files(
    before: SceneFile | str | Path | bytes,
    after: SceneFile | str | Path | bytes,
    scene: str,
    edits: Sequence[dict[str, Any]],
    ignore_children_of: Iterable[str] = (),
    added_defaults: Mapping[str, Mapping[str, str]] | None = None,
) -> list[Violation]:
    """What the saved file `after` shows against the file `before` (a `SceneFile`, a path or the bytes): see the module's docstring for the findings.

    SceneFileError when `before` cannot be read, has no scene `scene`, or an edit names something it does not have. A `after` that cannot be read or holds no
    scene is the one finding `editor.saved_file_unreadable`.
    """
    old = _load(before)
    old_scene = old.scene(scene)
    if old_scene is None:
        raise SceneFileError(f"the file before has no scene '{scene}' (it has {old.scene_names})")
    try:
        new = _load(after)
    except SceneFileError as error:
        return [Violation(NAME_FILE_UNREADABLE, str(error))]
    if not new.scenes:
        return [Violation(NAME_FILE_UNREADABLE, f"{new.path} holds no <scene>: the saved file does not load")]
    ignore = frozenset(ignore_children_of)
    defaults = added_defaults or {}
    found = _compare_root(old, new, ignore)
    found += _compare_scene_list(old, new, scene, ignore)
    found += _compare_edited_scene(old_scene, new.scene(scene), scene, edits, ignore, defaults)
    return found


class ContentGuard:
    """Puts content files back as they were: `arm()` before the editor starts, `release()` after it has quit (see the module's docstring)."""

    def __init__(self, paths: Iterable[str | Path], backup_dir: str | Path):
        self.paths = [Path(path).resolve() for path in paths]
        self.backup_dir = Path(backup_dir).expanduser().resolve()
        self._backups: dict[Path, Path | None] = {}
        self._armed = False

    @property
    def armed(self) -> bool:
        return self._armed

    def arm(self) -> None:
        if self._armed:
            raise ContentGuardError("the guard is armed already")
        if png_ops.inside_git_work_tree(self.backup_dir):
            raise ContentGuardError(f"{self.backup_dir} is inside a git work tree: the backup of a content file is never put in a repository; use a folder outside every repository")
        self.backup_dir.mkdir(parents=True, exist_ok=True)
        backups: dict[Path, Path | None] = {}
        try:
            for index, path in enumerate(self.paths):
                if path.is_file():
                    backup = self.backup_dir / BACKUP_FILE.format(index=index, name=path.name)
                    backup.write_bytes(path.read_bytes())
                    backups[path] = backup
                else:
                    backups[path] = None
        except OSError as error:
            raise ContentGuardError(f"cannot back up the content files into {self.backup_dir}: {error}") from error
        self._backups = backups
        self._armed = True

    def release(self) -> list[Path]:
        """Writes the backed-up bytes back where the file differs (removes a file that did not exist before) and gives the files it had to restore.

        Disarms the guard whatever happens; ContentGuardError, after every file was tried, when a file does not hold the old bytes afterwards.
        """
        if not self._armed:
            return []
        self._armed = False
        restored: list[Path] = []
        problems: list[str] = []
        for path, backup in self._backups.items():
            try:
                if _restore(path, backup):
                    restored.append(path)
            except OSError as error:
                problems.append(f"{path}: {error}")
        if problems:
            raise ContentGuardError("a content file could not be put back as it was: " + "; ".join(problems))
        return restored


def _restore(path: Path, backup: Path | None) -> bool:
    """Whether `path` had to be changed; ContentGuardError when it does not hold the old bytes afterwards."""
    if backup is None:
        if not path.exists():
            return False
        path.unlink()
        if path.exists():
            raise ContentGuardError(f"{path} did not exist before the session and could not be removed")
        return True
    wanted = backup.read_bytes()
    if path.is_file() and path.read_bytes() == wanted:
        return False
    path.write_bytes(wanted)
    if path.read_bytes() != wanted:
        raise ContentGuardError(f"{path} does not hold its old bytes after the restore")
    return True


def _load(source: SceneFile | str | Path | bytes) -> SceneFile:
    if isinstance(source, SceneFile):
        return source
    if isinstance(source, bytes):
        return parse_scene_file(source)
    return read_scene_file(source)


def _compare_root(old: SceneFile, new: SceneFile, ignore: frozenset[str]) -> list[Violation]:
    found: list[Violation] = []
    if old.root.tag != new.root.tag:
        return [Violation(NAME_ROOT_CHANGED, f"the root element was <{old.root.tag}>, now <{new.root.tag}>")]
    differences: list[tuple[str, str | None, str | None]] = []
    _diff_attributes(old.root, new.root, f"<{old.root.tag}>", differences)
    for tag in sorted({child.tag for child in (*old.root, *new.root)} - {SCENES_TAG}):
        _diff_element(old.root.find(tag), new.root.find(tag), f"<{old.root.tag}>/{tag}", ignore, differences)
    for path, before, after in differences:
        found.append(Violation(NAME_ROOT_CHANGED, _difference_text(path, before, after)))
    return found


def _compare_scene_list(old: SceneFile, new: SceneFile, edited: str, ignore: frozenset[str]) -> list[Violation]:
    found: list[Violation] = []
    old_keys, new_keys = _unique_keys(old.scene_names), _unique_keys(new.scene_names)
    by_old = dict(zip(old_keys, old.scenes))
    by_new = dict(zip(new_keys, new.scenes))
    for key in old_keys:
        if key not in by_new:
            found.append(Violation(NAME_OTHER_SCENE_CHANGED, f"the scene '{key}' vanished from the saved file"))
    for key in new_keys:
        if key not in by_old:
            found.append(Violation(NAME_OTHER_SCENE_CHANGED, f"the scene '{key}' is new in the saved file"))
    common_old = [key for key in old_keys if key in by_new]
    common_new = [key for key in new_keys if key in by_old]
    if common_old != common_new:
        found.append(Violation(NAME_OTHER_SCENE_CHANGED, f"the scenes are in another order: before {common_old}, after {common_new}"))
    for key in common_old:
        if key == edited:
            continue
        differences: list[tuple[str, str | None, str | None]] = []
        _diff_element(by_old[key], by_new[key], "", ignore, differences)
        if differences:
            found.append(Violation(NAME_OTHER_SCENE_CHANGED, f"the scene '{key}' changed: " + _summary(differences)))
    return found


def _compare_edited_scene(old_scene: ElementTree.Element, new_scene: ElementTree.Element | None, name: str, edits: Sequence[dict[str, Any]], ignore: frozenset[str], defaults: Mapping[str, Mapping[str, str]]) -> list[Violation]:
    if new_scene is None:
        return [Violation(NAME_EDIT_MISSING, f"the scene '{name}' is not in the saved file: {_edit_text(edit)}") for edit in edits]
    found: list[Violation] = []
    model = copy.deepcopy(old_scene)
    for edit in edits:
        trial = copy.deepcopy(model)
        _apply_edit(trial, new_scene, edit)
        problem = _edit_problem(new_scene, edit)
        if problem is None:
            model = trial
        else:
            found.append(Violation(NAME_EDIT_MISSING, f"scene '{name}': {_edit_text(edit)}: {problem}"))
    differences: list[tuple[str, str | None, str | None]] = []
    _diff_element(model, new_scene, "", ignore, differences, defaults)
    for path, before, after in differences[:MAX_DIFFERENCES]:
        found.append(Violation(NAME_SCENE_CHANGED, f"scene '{name}': {_difference_text(path, before, after)}"))
    if len(differences) > MAX_DIFFERENCES:
        found.append(Violation(NAME_SCENE_CHANGED, f"scene '{name}': {len(differences) - MAX_DIFFERENCES} more differences"))
    return found


def _apply_edit(model: ElementTree.Element, saved: ElementTree.Element, edit: dict[str, Any]) -> None:
    """Applies the edit to `model`, a copy of the scene before; SceneFileError when the request names something it does not have.

    A node-added edit takes the node from the saved scene (when it is there), so what Save wrote under it is not judged: only that it exists.
    """
    kind, path = edit.get("kind"), str(edit.get("path", ""))
    nodes = _node_index(model)
    if kind == EDIT_NODE_ADDED:
        if path in nodes:
            raise SceneFileError(f"{_edit_text(edit)}: the node exists in the file before")
        added = _node_index(saved).get(path)
        if added is None:
            return
        parent_path, _, _ = path.rpartition(PATH_SEPARATOR)
        parent = nodes.get(parent_path) if parent_path else model
        if parent is None:
            raise SceneFileError(f"{_edit_text(edit)}: the parent '{parent_path}' is not in the file before")
        _children_wrapper(parent).append(copy.deepcopy(added))
        return
    node = nodes.get(path)
    if node is None:
        raise SceneFileError(f"{_edit_text(edit)}: no node '{path}' in the file before")
    if kind == EDIT_ATTRIBUTE:
        node.set(str(edit["name"]), str(edit["value"]))
    elif kind == EDIT_COMPONENT_ATTRIBUTE:
        component = _component(node, str(edit["component"]))
        if component is None:
            raise SceneFileError(f"{_edit_text(edit)}: the node has no component '{edit['component']}' in the file before")
        component.set(str(edit["name"]), str(edit["value"]))
    elif kind == EDIT_NODE_NAME:
        node.set(NAME_ATTRIBUTE, str(edit["new_name"]))
    else:
        raise SceneFileError(f"unknown edit kind {kind!r}: {_edit_text(edit)}")


def _edit_problem(saved: ElementTree.Element, edit: dict[str, Any]) -> str | None:
    """Why the edit is not in the saved scene, or None."""
    kind, path = edit["kind"], str(edit["path"])
    saved_nodes = _node_index(saved)
    if kind == EDIT_NODE_ADDED:
        return None if path in saved_nodes else f"no node '{path}' in the saved scene"
    if kind == EDIT_NODE_NAME:
        parent, _, _ = path.rpartition(PATH_SEPARATOR)
        new_path = f"{parent}{PATH_SEPARATOR}{edit['new_name']}" if parent else str(edit["new_name"])
        return None if new_path in saved_nodes else f"no node '{new_path}' in the saved scene"
    node = saved_nodes.get(path)
    if node is None:
        return f"no node '{path}' in the saved scene"
    name, wanted = str(edit["name"]), str(edit["value"])
    if kind == EDIT_COMPONENT_ATTRIBUTE:
        component = _component(node, str(edit["component"]))
        if component is None:
            return f"the node has no component '{edit['component']}' in the saved scene"
        node = component
    actual = node.get(name)
    return None if actual == wanted else f"'{name}' is {_shown(actual)} in the saved scene, not {wanted!r}"


def _node_index(scene: ElementTree.Element) -> dict[str, ElementTree.Element]:
    """Every node of a scene by path (names from the scene down, a repeated sibling name `name[2]`)."""
    index: dict[str, ElementTree.Element] = {}

    def walk(holder: ElementTree.Element, prefix: str) -> None:
        for wrapper in holder:
            if wrapper.tag not in (NODES_TAG, CHILDREN_TAG):
                continue
            nodes = [child for child in wrapper if child.tag == NODE_TAG]
            for key, node in zip(_unique_keys([_node_label(node) for node in nodes]), nodes):
                path = f"{prefix}{PATH_SEPARATOR}{key}" if prefix else key
                index[path] = node
                walk(node, path)

    walk(scene, "")
    return index


def _children_wrapper(parent: ElementTree.Element) -> ElementTree.Element:
    tag = NODES_TAG if parent.tag == SCENE_TAG else CHILDREN_TAG
    wrapper = parent.find(tag)
    if wrapper is None:
        wrapper = ElementTree.SubElement(parent, tag)
    return wrapper


def _component(node: ElementTree.Element, type_name: str) -> ElementTree.Element | None:
    wrapper = node.find(COMPONENTS_TAG)
    if wrapper is None:
        return None
    return next((component for component in wrapper if component.get(TYPE_ATTRIBUTE) == type_name), None)


def _diff_attributes(old: ElementTree.Element, new: ElementTree.Element, path: str, out: list[tuple[str, str | None, str | None]], defaults: Mapping[str, str] | None = None) -> None:
    for key in sorted(set(old.attrib) | set(new.attrib)):
        if old.get(key) is None and defaults is not None and defaults.get(key) == new.get(key):
            continue
        if old.get(key) != new.get(key):
            out.append((f"{path}@{key}", old.get(key), new.get(key)))


def _diff_element(
    old: ElementTree.Element | None,
    new: ElementTree.Element | None,
    path: str,
    ignore: frozenset[str],
    out: list[tuple[str, str | None, str | None]],
    defaults: Mapping[str, Mapping[str, str]] | None = None,
) -> None:
    """Appends (path, before, after) for every difference of two elements: attributes, text, and the children matched by their identity.

    The `children` of a node named in `ignore` are left out on both sides. A child that is in one only, or in another order, is a difference at its path.
    """
    if old is None or new is None:
        if old is not None or new is not None:
            out.append((path, None if old is None else f"<{old.tag}>", None if new is None else f"<{new.tag}>"))
        return
    if old.tag != new.tag:
        out.append((path, f"<{old.tag}>", f"<{new.tag}>"))
        return
    allowed = defaults.get(old.get(TYPE_ATTRIBUTE) or "") if defaults and old.tag == COMPONENT_TAG else None
    _diff_attributes(old, new, path, out, allowed)
    old_text, new_text = (old.text or "").strip(), (new.text or "").strip()
    if old_text != new_text:
        out.append((f"{path}#text", old_text or None, new_text or None))
    skip_children = old.tag == NODE_TAG and old.get(NAME_ATTRIBUTE) in ignore
    old_children = [child for child in old if not (skip_children and child.tag == CHILDREN_TAG)]
    new_children = [child for child in new if not (skip_children and child.tag == CHILDREN_TAG)]
    old_keys = _unique_keys([_element_key(child) for child in old_children])
    new_keys = _unique_keys([_element_key(child) for child in new_children])
    by_old, by_new = dict(zip(old_keys, old_children)), dict(zip(new_keys, new_children))
    for key in old_keys:
        if key not in by_new:
            out.append((_child_path(path, by_old[key], key, present=False), "present", None))
    for key in new_keys:
        if key not in by_old:
            out.append((_child_path(path, by_new[key], key, present=False), None, "present"))
    common_old, common_new = [key for key in old_keys if key in by_new], [key for key in new_keys if key in by_old]
    if common_old != common_new:
        out.append((f"{path or '<scene>'} (order)", ", ".join(common_old), ", ".join(common_new)))
    for key in common_old:
        _diff_element(by_old[key], by_new[key], _child_path(path, by_old[key], key), ignore, out, defaults)


def _element_key(element: ElementTree.Element) -> str:
    """The identity of a child among its siblings: the name of a node or scene, the type of a component, the tag of anything else."""
    if element.tag in (NODE_TAG, SCENE_TAG):
        return element.get(NAME_ATTRIBUTE) or element.get(REF_ATTRIBUTE) or element.tag
    if element.tag == COMPONENT_TAG:
        return element.get(TYPE_ATTRIBUTE) or element.tag
    return element.tag


def _child_path(parent: str, child: ElementTree.Element, key: str, present: bool = True) -> str:
    if child.tag in WRAPPER_TAGS and not child.attrib and present:
        return parent  # a wrapper has the path of its holder: Field/TransformComponent, not Field/components/TransformComponent
    return f"{parent}{PATH_SEPARATOR}{key}" if parent else key


def _node_label(node: ElementTree.Element) -> str:
    return node.get(NAME_ATTRIBUTE) or node.get(REF_ATTRIBUTE) or NODE_TAG


def _unique_keys(names: Sequence[str]) -> list[str]:
    """The names with a repeat marked: the second `a` is `a[2]`."""
    seen: Counter[str] = Counter()
    keys = []
    for name in names:
        seen[name] += 1
        keys.append(name if seen[name] == 1 else f"{name}[{seen[name]}]")
    return keys


def _summary(differences: Sequence[tuple[str, str | None, str | None]]) -> str:
    shown = "; ".join(_difference_text(*difference) for difference in differences[:3])
    return shown + (f"; {len(differences) - 3} more" if len(differences) > 3 else "")


def _difference_text(path: str, before: str | None, after: str | None) -> str:
    return f"{path or '<scene>'}: before {_shown(before)}, after {_shown(after)}"


def _shown(value: str | None) -> str:
    return ABSENT if value is None else repr(value)


def _edit_text(edit: dict[str, Any]) -> str:
    return "edit " + ", ".join(f"{key}={value!r}" for key, value in edit.items())
