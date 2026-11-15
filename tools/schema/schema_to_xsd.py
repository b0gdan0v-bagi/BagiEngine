#!/usr/bin/env python3
"""Turns the JSON of the control method `schema` into an XSD 1.0 per config file, so an editor completes and checks `config/*.xml`.

    schema_to_xsd.py schema.json --config-dir config --out-dir xsd [--bind Stem=Type[@element]] [--vscode-settings] [--check]

Every `config/<Stem>.xml` whose type is known gets `<out-dir>/<Stem>.xsd`; the others are listed with the reason ("not covered").
The type of a file is, in this order: a `--bind` for its stem; the type named like the stem; the type named like its root element.
`Type@element` says the type sits one element down (`<root><window width=""/></root>` is `SDLWindowConfig@window`).

The XSD follows the XML the serializers write (schema_common.py). XSD 1.0 cannot say "the attributes of this element depend on its
`type`", so a factory element is declared once per factory base with the union of the attributes and children of its derived types, all
optional: it catches an unknown name or value, not an attribute that belongs to another derived type. A field the schema describes as
`opaque` (a class with a hand-written Serialize) is any content. Standard library only.
"""

import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from schema_common import (  # noqa: E402
    CONFIG_EXTENSION,
    CONTENT_ROOTS,
    CONTENT_VERSION_ATTRIBUTE,
    ENABLED_ATTRIBUTE,
    ENTRY_ELEMENT,
    EXIT_BAD_USAGE,
    EXIT_FAILED,
    EXIT_PASSED,
    EXIT_SKIPPED,
    INDEX_ATTRIBUTE,
    INTEGER_KINDS,
    ITEM_ELEMENT,
    KEY_ATTRIBUTE,
    NODE_REFERENCE_ATTRIBUTE,
    NODE_TYPE,
    PAIR_FIRST,
    PAIR_SECOND,
    SCALAR_XSD_TYPES,
    SCHEMA_VERSION,
    TYPE_ATTRIBUTE,
    VALUE_NAME,
    XS_ANY_TYPE,
    XS_BOOLEAN,
    XS_INTEGER,
    XS_NAMESPACE,
    XS_PREFIX,
    XS_STRING,
    XS_UNSIGNED_INTEGER,
    XSD_EXTENSION,
)

XML_DECLARATION = '<?xml version="1.0" encoding="UTF-8"?>\n'
BIND_SEPARATOR = "="
WRAPPER_SEPARATOR = "@"
NCNAME_FORBIDDEN = re.compile(r"[^A-Za-z0-9_.-]")
WORD = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z][a-z0-9]*|[A-Z]+|[a-z0-9]+")
XMLLINT = "xmllint"
XMLLINT_SUMMARY_ENDINGS = ("validates", "fails to validate")  # the closing line of a report, which says nothing about where
XMLLINT_ERROR_LIMIT = 5  # lines of xmllint's report that go into a failure message


class SchemaError(Exception):
    """The schema JSON is not what `schema_to_xsd` reads."""


@dataclass(frozen=True)
class Attribute:
    name: str
    simple: str  # an `xs:` type or the name of a simple type of the XSD
    required: bool = False
    doc: str = ""


@dataclass(frozen=True)
class Child:
    name: str
    complex: str  # the name of a complex type of the XSD, or `xs:anyType`
    required: bool = False
    doc: str = ""


@dataclass(frozen=True)
class Content:
    """What an element holds: attributes and child elements. `repeated`: one child element, any number of times, in a sequence."""

    attributes: tuple[Attribute, ...] = ()
    children: tuple[Child, ...] = ()
    repeated: bool = False


@dataclass(frozen=True)
class SimpleRestriction:
    base: str
    enumeration: tuple[str, ...] = ()
    minimum: str | None = None
    maximum: str | None = None
    max_length: int | None = None


@dataclass(frozen=True)
class Binding:
    type_name: str
    wrapper: str | None = None  # the element of the file's root that holds the type's fields


@dataclass
class FileResult:
    stem: str
    root: str | None
    type_name: str | None
    reason: str = ""
    xsd: Path | None = None


def conventional_element_name(base: str) -> str:
    """The element a list of a factory base is written with when the schema has no `elementName`: the last word of the class name, lower case.

    `IComponent` is `component`, `SceneNode` is `node`: what the meta generator makes of the class names.
    """
    words = WORD.findall(base)
    return words[-1].lower() if words else base.lower()


def ncname(text: str) -> str:
    cleaned = NCNAME_FORBIDDEN.sub("_", text)
    return cleaned if cleaned and (cleaned[0].isalpha() or cleaned[0] == "_") else f"_{cleaned}"


def number_text(value: float, integer: bool, round_up: bool) -> str:
    if integer:
        return str(math.ceil(value) if round_up else math.floor(value))
    return str(int(value)) if float(value).is_integer() else repr(float(value))


def check_version(schema: dict[str, Any]) -> None:
    if schema.get("version") != SCHEMA_VERSION:
        raise SchemaError(f"the schema has version {schema.get('version')!r}, this tool reads {SCHEMA_VERSION}")


class XsdBuilder:
    """The XSD of one config file: the complex types reachable from its root type, named and shared."""

    def __init__(self, schema: dict[str, Any]) -> None:
        check_version(schema)
        self.types: dict[str, dict[str, Any]] = {entry["name"]: entry for entry in schema.get("types", [])}
        self.factories: dict[str, dict[str, Any]] = {entry["base"]: entry for entry in schema.get("factories", [])}
        self.complex_types: dict[str, Content | None] = {}  # None: reserved, being made (a type that holds itself)
        self.simple_types: dict[str, SimpleRestriction] = {}
        self._complex_names: dict[Content, str] = {}
        self._simple_names: dict[SimpleRestriction, str] = {}

    # ---- naming and sharing

    def _intern_complex(self, content: Content) -> str:
        if not content.attributes and not content.children:
            content = Content()
        name = self._complex_names.get(content)
        if name is None:
            name = f"anon{len(self._complex_names) + 1}"
            self._complex_names[content] = name
            self.complex_types[name] = content
        return name

    def _intern_simple(self, restriction: SimpleRestriction, prefix: str) -> str:
        name = self._simple_names.get(restriction)
        if name is None:
            digest = hashlib.sha1(repr(restriction).encode("utf-8")).hexdigest()[:8]
            name = f"{prefix}_{digest}"
            self._simple_names[restriction] = name
            self.simple_types[name] = restriction
        return name

    # ---- simple values: one attribute

    def _simple_for(self, desc: dict[str, Any], attributes: dict[str, Any]) -> str | None:
        kind = desc.get("kind")
        if kind == "enum":
            return self._intern_simple(SimpleRestriction(XS_STRING, enumeration=tuple(desc.get("values", []))), "enum")
        base = SCALAR_XSD_TYPES.get(kind)
        if base is None:
            return None
        restriction = SimpleRestriction(base)
        if kind == "string" and desc.get("capacity"):
            restriction = SimpleRestriction(base, max_length=int(desc["capacity"]))
        if kind != "bool" and kind != "string" and "range" in attributes:
            low, high = attributes["range"]
            integer = kind in INTEGER_KINDS
            restriction = SimpleRestriction(base, minimum=number_text(low, integer, True), maximum=number_text(high, integer, False))
        if restriction == SimpleRestriction(base):
            return base
        return self._intern_simple(restriction, "limited")

    # ---- members of a type

    def _members_of_type(self, name: str) -> tuple[list[Attribute], list[Child]]:
        """The attributes and children of a type with those of its parents (the parent's first); a later field of a name replaces an earlier."""
        chain: list[dict[str, Any]] = []
        seen: set[str] = set()
        current: dict[str, Any] | None = self.types.get(name)
        while current is not None and current["name"] not in seen:
            seen.add(current["name"])
            chain.append(current)
            parent = current.get("parent")
            current = self.types.get(parent) if parent else None
        attributes: dict[str, Attribute] = {}
        children: dict[str, Child] = {}
        for entry in reversed(chain):
            for item in entry.get("fields", []):
                member = self._member(item["name"], item["type"], item.get("attributes", {}), item.get("attributes", {}).get("required", False))
                if isinstance(member, Attribute):
                    attributes[member.name] = member
                    children.pop(member.name, None)
                else:
                    children[member.name] = member
        if name == NODE_TYPE:
            attributes[NODE_REFERENCE_ATTRIBUTE] = Attribute(NODE_REFERENCE_ATTRIBUTE, XS_STRING)  # `<node ref="..."/>`, expanded by the loader
        return list(attributes.values()), list(children.values())

    def _member(self, name: str, desc: dict[str, Any], attributes: dict[str, Any], required: bool) -> Attribute | Child:
        doc = attributes.get("tooltip", "")
        if desc.get("kind") == "optional" and "element" in desc:
            return self._member(name, desc["element"], attributes, False)
        simple = self._simple_for(desc, attributes)
        if simple is not None:
            return Attribute(name, simple, required, doc)
        return Child(name, self._complex_for(desc, attributes), required, doc)

    # ---- element bodies

    def _object_type(self, name: str) -> str:
        type_name = f"type_{ncname(name)}"
        if type_name not in self.complex_types:
            self.complex_types[type_name] = None
            attributes, children = self._members_of_type(name)
            self.complex_types[type_name] = Content(tuple(attributes), tuple(children))
        return type_name

    def _factory_type(self, base: str) -> str:
        type_name = f"factory_{ncname(base)}"
        if type_name not in self.complex_types:
            self.complex_types[type_name] = None
            self.complex_types[type_name] = self._factory_content(base)
        return type_name

    def _factory_content(self, base: str) -> Content:
        derived = sorted(name for name in self.factories[base].get("derived", []) if self.types.get(name, {}).get("creatable"))
        type_simple = self._intern_simple(SimpleRestriction(XS_STRING, enumeration=tuple(derived)), "enum") if derived else XS_STRING
        # A factory that can make its own base (`<node/>` for SceneNode) makes it when `type` is missing; an abstract one cannot
        type_required = bool(self.types.get(base, {}).get("abstract", True))
        attributes: dict[str, Attribute] = {
            TYPE_ATTRIBUTE: Attribute(TYPE_ATTRIBUTE, type_simple, required=type_required),
            ENABLED_ATTRIBUTE: Attribute(ENABLED_ATTRIBUTE, XS_BOOLEAN),
        }
        children: dict[str, Child] = {}
        for name in derived:
            own_attributes, own_children = self._members_of_type(name)
            for attribute in own_attributes:
                known = attributes.get(attribute.name)
                optional = Attribute(attribute.name, attribute.simple, False, attribute.doc)
                if attribute.name in (TYPE_ATTRIBUTE, ENABLED_ATTRIBUTE):
                    continue
                if known is None:
                    attributes[attribute.name] = optional
                elif known.simple != attribute.simple:
                    attributes[attribute.name] = Attribute(attribute.name, XS_STRING, False, known.doc)
            for child in own_children:
                known_child = children.get(child.name)
                optional_child = Child(child.name, child.complex, False, child.doc)
                if known_child is None:
                    children[child.name] = optional_child
                elif known_child.complex != child.complex:
                    children[child.name] = Child(child.name, XS_ANY_TYPE, False, known_child.doc)
        return Content(tuple(attributes.values()), tuple(children.values()))

    def _pointee(self, desc: dict[str, Any]) -> str | None:
        name = desc.get("object")
        return name if name in self.types or name in self.factories else None

    def _complex_for(self, desc: dict[str, Any], attributes: dict[str, Any]) -> str:
        """The complex type of the element a field of this type is: its own element in the parent."""
        kind = desc.get("kind")
        if kind in ("object", "pointer"):
            name = self._pointee(desc)
            if name is None:
                return XS_ANY_TYPE
            return self._factory_type(name) if name in self.factories else self._object_type(name)
        if kind == "list":
            return self._intern_complex(Content(children=(self._list_child(desc.get("element"), attributes),), repeated=True))
        if kind == "map":
            return self._intern_complex(Content(children=(Child(ENTRY_ELEMENT, self._entry_type(desc, attributes)),), repeated=True))
        content = self._body(desc, attributes)
        return XS_ANY_TYPE if content is None else self._intern_complex(content)

    def _list_child(self, element: dict[str, Any] | None, attributes: dict[str, Any]) -> Child:
        if element is None:
            return Child(ITEM_ELEMENT, XS_ANY_TYPE)
        if element.get("kind") == "pointer":
            base = self._pointee(element)
            if base is not None and base in self.factories:
                name = self.factories[base].get("elementName") or conventional_element_name(base)
                return Child(name, self._factory_type(base))
        return Child(ITEM_ELEMENT, self._body_type(element, attributes))

    def _entry_type(self, desc: dict[str, Any], attributes: dict[str, Any]) -> str:
        key = desc.get("key") or {}
        key_simple = self._simple_for(key, {}) or XS_STRING
        key_attribute = Attribute(KEY_ATTRIBUTE, key_simple, required=True)
        content = self._body(desc.get("element") or {}, attributes)
        if content is None:
            return XS_ANY_TYPE
        return self._intern_complex(Content((key_attribute, *content.attributes), content.children, content.repeated))

    def _body_type(self, desc: dict[str, Any], attributes: dict[str, Any]) -> str:
        """The type of an element that holds a value as its own body (an item of a list)."""
        kind = desc.get("kind")
        if kind in ("object", "pointer", "list", "map"):
            return self._complex_for(desc, attributes)
        content = self._body(desc, attributes)
        return XS_ANY_TYPE if content is None else self._intern_complex(content)

    def _body(self, desc: dict[str, Any], attributes: dict[str, Any]) -> Content | None:
        """What an element holds when it is the value itself (an item, an entry, an alternative): None for any content."""
        kind = desc.get("kind")
        simple = self._simple_for(desc, attributes)
        if simple is not None:
            return Content((Attribute(VALUE_NAME, simple),))
        if kind == "optional":
            return self._body(desc.get("element") or {}, attributes)
        if kind in ("object", "pointer"):
            name = self._pointee(desc)
            if name is None:
                return None
            if name in self.factories:
                return self._factory_content(name)
            members = self._members_of_type(name)
            return Content(tuple(members[0]), tuple(members[1]))
        if kind == "pair":
            first = self._member(PAIR_FIRST, desc.get("key") or {}, attributes, False)
            second = self._member(PAIR_SECOND, desc.get("element") or {}, attributes, False)
            return Content(tuple(m for m in (first, second) if isinstance(m, Attribute)), tuple(m for m in (first, second) if isinstance(m, Child)))
        if kind == "variant":
            return self._variant_content(desc, attributes)
        return None

    def _variant_content(self, desc: dict[str, Any], attributes: dict[str, Any]) -> Content:
        alternatives = desc.get("alternatives", [])
        index = self._intern_simple(SimpleRestriction(XS_INTEGER, minimum="0", maximum=str(max(len(alternatives) - 1, 0))), "limited")
        scalar_types = {simple for alt in alternatives if (simple := self._simple_for(alt, attributes)) is not None}
        child_types = {self._complex_for(alt, attributes) for alt in alternatives if self._simple_for(alt, attributes) is None and alt.get("kind") not in ("opaque", "unknown", "void")}
        children: list[Child] = []
        attributes_out = [Attribute(INDEX_ATTRIBUTE, index, required=True)]
        if scalar_types:
            attributes_out.append(Attribute(VALUE_NAME, next(iter(scalar_types)) if len(scalar_types) == 1 else XS_STRING))
        if child_types:
            children.append(Child(VALUE_NAME, next(iter(child_types)) if len(child_types) == 1 else XS_ANY_TYPE))
        return Content(tuple(attributes_out), tuple(children))

    # ---- one config file

    def root_type(self, type_name: str, root_element: str, wrapper: str | None) -> str:
        """The complex type of the file's root element: the type's own content (or the wrapper element holding it) plus `contentVersion`."""
        if type_name not in self.types:
            raise SchemaError(f"the schema has no type '{type_name}'")
        if wrapper is None:
            attributes, children = self._members_of_type(type_name)
        else:
            attributes, children = [], [Child(wrapper, self._object_type(type_name))]
        if root_element in CONTENT_ROOTS:
            attributes.append(Attribute(CONTENT_VERSION_ATTRIBUTE, XS_UNSIGNED_INTEGER))
        name = f"root_{ncname(type_name)}"
        self.complex_types[name] = Content(tuple(attributes), tuple(children))
        return name

    def render(self, root_element: str, root_type: str) -> str:
        ET.register_namespace(XS_PREFIX, XS_NAMESPACE)
        schema = ET.Element(f"{{{XS_NAMESPACE}}}schema")
        ET.SubElement(schema, f"{{{XS_NAMESPACE}}}element", {"name": root_element, "type": root_type})
        for name in sorted(self.simple_types):
            self._render_simple(schema, name, self.simple_types[name])
        for name in sorted(self.complex_types):
            content = self.complex_types[name]
            if content is not None:
                self._render_complex(schema, name, content)
        ET.indent(schema, space="  ")
        return XML_DECLARATION + ET.tostring(schema, encoding="unicode") + "\n"

    @staticmethod
    def _render_simple(schema: ET.Element, name: str, restriction: SimpleRestriction) -> None:
        simple = ET.SubElement(schema, f"{{{XS_NAMESPACE}}}simpleType", {"name": name})
        node = ET.SubElement(simple, f"{{{XS_NAMESPACE}}}restriction", {"base": restriction.base})
        for value in restriction.enumeration:
            ET.SubElement(node, f"{{{XS_NAMESPACE}}}enumeration", {"value": value})
        if restriction.minimum is not None:
            ET.SubElement(node, f"{{{XS_NAMESPACE}}}minInclusive", {"value": restriction.minimum})
        if restriction.maximum is not None:
            ET.SubElement(node, f"{{{XS_NAMESPACE}}}maxInclusive", {"value": restriction.maximum})
        if restriction.max_length is not None:
            ET.SubElement(node, f"{{{XS_NAMESPACE}}}maxLength", {"value": str(restriction.max_length)})

    @staticmethod
    def _annotate(parent: ET.Element, doc: str) -> None:
        if doc:
            annotation = ET.SubElement(parent, f"{{{XS_NAMESPACE}}}annotation")
            ET.SubElement(annotation, f"{{{XS_NAMESPACE}}}documentation").text = doc

    def _render_complex(self, schema: ET.Element, name: str, content: Content) -> None:
        node = ET.SubElement(schema, f"{{{XS_NAMESPACE}}}complexType", {"name": name})
        if content.children:
            group = ET.SubElement(node, f"{{{XS_NAMESPACE}}}sequence" if content.repeated else f"{{{XS_NAMESPACE}}}all")
            for child in content.children:
                attrs = {"name": child.name, "type": child.complex}
                if content.repeated:
                    attrs.update({"minOccurs": "0", "maxOccurs": "unbounded"})
                else:
                    attrs["minOccurs"] = "1" if child.required else "0"
                element = ET.SubElement(group, f"{{{XS_NAMESPACE}}}element", attrs)
                self._annotate(element, child.doc)
        for attribute in content.attributes:
            attrs = {"name": attribute.name, "type": attribute.simple, "use": "required" if attribute.required else "optional"}
            element = ET.SubElement(node, f"{{{XS_NAMESPACE}}}attribute", attrs)
            self._annotate(element, attribute.doc)


def build_xsd(schema: dict[str, Any], type_name: str, root_element: str, wrapper: str | None = None) -> str:
    """The XSD text of a file whose root element is `root_element` and whose content is `type_name` (inside `wrapper` when given)."""
    builder = XsdBuilder(schema)
    return builder.render(root_element, builder.root_type(type_name, root_element, wrapper))


def parse_binding(text: str) -> tuple[str, Binding]:
    """`Stem=Type` or `Stem=Type@element`."""
    stem, separator, rest = text.partition(BIND_SEPARATOR)
    type_name, _, wrapper = rest.partition(WRAPPER_SEPARATOR)
    if not separator or not stem or not type_name:
        raise ValueError(f"a binding is Stem=Type or Stem=Type{WRAPPER_SEPARATOR}element, not '{text}'")
    return stem, Binding(type_name, wrapper or None)


def root_element_of(path: Path) -> str:
    return ET.parse(path).getroot().tag


def resolve_type(stem: str, root: str, types: set[str], bindings: dict[str, Binding]) -> tuple[Binding | None, str]:
    """The type of a config file and, when there is none, why."""
    if stem in bindings:
        binding = bindings[stem]
        if binding.type_name in types:
            return binding, ""
        return None, f"bound to '{binding.type_name}', which the schema does not have"
    if stem in types:
        return Binding(stem), ""
    if root in types:
        return Binding(root), ""
    return None, f"no type named '{stem}' or '{root}': read by hand"


def convert_config_dir(schema: dict[str, Any], config_dir: Path, out_dir: Path, bindings: dict[str, Binding] | None = None) -> list[FileResult]:
    """One XSD per covered `<config_dir>/<Stem>.xml` in `out_dir`; every file is in the answer, covered or not, in the order of the names."""
    check_version(schema)
    bindings = bindings or {}
    types = {entry["name"] for entry in schema.get("types", [])}
    out_dir.mkdir(parents=True, exist_ok=True)
    results: list[FileResult] = []
    for path in sorted(config_dir.glob(f"*{CONFIG_EXTENSION}")):
        try:
            root = root_element_of(path)
        except ET.ParseError as error:
            results.append(FileResult(path.stem, None, None, f"does not parse: {error}"))
            continue
        binding, reason = resolve_type(path.stem, root, types, bindings)
        if binding is None:
            results.append(FileResult(path.stem, root, None, reason))
            continue
        target = out_dir / f"{path.stem}{XSD_EXTENSION}"
        target.write_text(build_xsd(schema, binding.type_name, root, binding.wrapper), encoding="utf-8")
        results.append(FileResult(path.stem, root, binding.type_name, "", target))
    return results


def vscode_settings(results: list[FileResult], config_dir: Path) -> str:
    """The `xml.fileAssociations` block of the VS Code XML extension (Red Hat's `redhat.vscode-xml`) for the covered files."""
    associations = [{"pattern": f"**/{config_dir.resolve().name}/{result.stem}{CONFIG_EXTENSION}", "systemId": str(result.xsd.resolve())} for result in results if result.xsd is not None]
    return '"xml.fileAssociations": ' + json.dumps(associations, indent=2)


def find_xmllint() -> str | None:
    return shutil.which(XMLLINT)


def validate_with_xmllint(xsd: Path, xml: Path) -> list[str]:
    """The lines xmllint reports for `xml` against `xsd`; empty when it is valid. xmllint has to exist (find_xmllint)."""
    process = subprocess.run([XMLLINT, "--noout", "--schema", str(xsd), str(xml)], capture_output=True, text=True, check=False)
    if process.returncode == 0:
        return []
    lines = [line for line in process.stderr.splitlines() if line.strip() and not line.endswith(XMLLINT_SUMMARY_ENDINGS)]
    return lines or [f"{XMLLINT} exited with {process.returncode}"]


def check_files(results: list[FileResult], config_dir: Path) -> dict[str, list[str]]:
    """The findings of every covered file, by stem (only the files that have some)."""
    findings: dict[str, list[str]] = {}
    for result in results:
        if result.xsd is None:
            continue
        lines = validate_with_xmllint(result.xsd, config_dir / f"{result.stem}{CONFIG_EXTENSION}")
        if lines:
            findings[result.stem] = lines[:XMLLINT_ERROR_LIMIT]
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("schema", type=Path, help="the JSON of the `schema` method (export_schema.py writes it)")
    parser.add_argument("--config-dir", type=Path, required=True, help="the folder of the config files, e.g. config")
    parser.add_argument("--out-dir", type=Path, required=True, help="where the .xsd files go")
    parser.add_argument("--bind", action="append", default=[], metavar="Stem=Type[@element]", help="the type of a file whose stem is not a type's name")
    parser.add_argument("--vscode-settings", action="store_true", help="print the xml.fileAssociations block for VS Code")
    parser.add_argument("--check", action="store_true", help=f"validate every covered file with {XMLLINT} (exit 1 on a finding, 77 without {XMLLINT})")
    args = parser.parse_args()

    try:
        bindings = dict(parse_binding(text) for text in args.bind)
    except ValueError as error:
        parser.error(str(error))
    try:
        schema = json.loads(args.schema.read_text(encoding="utf-8"))
        results = convert_config_dir(schema, args.config_dir, args.out_dir, bindings)
    except (OSError, ValueError, SchemaError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_BAD_USAGE

    covered = [result for result in results if result.xsd is not None]
    for result in results:
        print(f"covered {result.stem}.xml: {result.type_name} -> {result.xsd}" if result.xsd else f"not covered {result.stem}.xml: {result.reason}")
    print(f"covered {len(covered)}, not covered {len(results) - len(covered)}")
    if args.vscode_settings:
        print(vscode_settings(results, args.config_dir))
    if args.check:
        if find_xmllint() is None:
            print(f"SKIPPED: {XMLLINT} is not on PATH")
            return EXIT_SKIPPED
        findings = check_files(results, args.config_dir)
        for stem, lines in findings.items():
            print(f"INVALID {stem}.xml:\n  " + "\n  ".join(lines))
        return EXIT_FAILED if findings else EXIT_PASSED
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
