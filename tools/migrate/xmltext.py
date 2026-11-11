"""A lossless XML model for the migration tool.

A file is parsed into a tree that remembers the source text of every part, and writing it back gives the same bytes when nothing
was changed. Only what an edit touches changes: the start tag of an edited element is rendered again from its parts (the
whitespace before every attribute, the name, the text between the name and the quote, the quote, the raw value, the whitespace
before the closing bracket), and every part that was not edited keeps its source text. Comments, CDATA, processing instructions,
entities, line endings, a byte order mark, tabs and a missing final newline are kept as they were.

The model:

- `Document`: the raw `prolog` (everything before the root element), the `root` and the raw `epilog` (everything after it).
- `Element`: `name`, ordered attributes, `children` (`Element` and `RawNode` of kind text, comment, cdata or pi), `parent`,
  `self_closing`, and `line` / `column` of its start tag in the source (None for an element a step created).
- Attribute values are kept as written (`&quot;`, `&amp;` stay). `get` returns the decoded value; `set` escapes what it writes and
  leaves an attribute whose decoded value is already the new one untouched.

A step edits through `get`, `set`, `remove`, `iter`, `find_all`, `add_child`, `remove_child` and `replace_children`. The model is
about text, not about XML semantics: it does not validate names against the XML grammar beyond what it needs to find the tags, does
not expand entities and does not read a DTD (a DOCTYPE is kept as raw prolog).
"""

from __future__ import annotations

import bisect
import re
from typing import Callable, Iterator, List, Optional, Union

__all__ = ["XmlTextError", "Document", "Element", "RawNode", "parse", "parse_bytes", "read", "escape_attribute"]


class XmlTextError(Exception):
    """The file is not well-formed XML as far as this model can tell. `line` and `column` are 1-based."""

    def __init__(self, message: str, line: int, column: int) -> None:
        super().__init__(f"{line}:{column}: {message}")
        self.message = message
        self.line = line
        self.column = column


_WHITESPACE = " \t\r\n"
_NAME = re.compile(r"(?:[^\W\d]|:)[\w:.\-]*")
_BOM = "﻿"
_ENTITY = re.compile(r"&(?:#x([0-9A-Fa-f]+)|#([0-9]+)|(lt|gt|amp|quot|apos));")
_NAMED_ENTITIES = {"lt": "<", "gt": ">", "amp": "&", "quot": '"', "apos": "'"}
_INDENT_AFTER_NEWLINE = re.compile(r"\n([ \t]*)\Z")
_INDENT_OF_PROLOG = re.compile(r"(?:\A|\n)([ \t]*)\Z")
_DEFAULT_UNIT = "  "
_DEFAULT_NEWLINE = "\n"
_DEFAULT_TAIL_OF_OPENED_EMPTY = " "


def _decode_attribute(raw: str) -> str:
    """The value a parser reads: line breaks and tabs become spaces, then the entities are resolved."""
    raw = re.sub(r"\r\n|[\r\n\t]", " ", raw)

    def replace(match: "re.Match[str]") -> str:
        if match.group(1) is not None:
            return chr(int(match.group(1), 16))
        if match.group(2) is not None:
            return chr(int(match.group(2)))
        return _NAMED_ENTITIES[match.group(3)]

    return _ENTITY.sub(replace, raw)


def escape_attribute(value: str, quote: str = '"') -> str:
    """The raw text of an attribute value written between `quote` marks: `&`, `<`, the quote and the whitespace a parser would fold."""
    out = value.replace("&", "&amp;").replace("<", "&lt;")
    out = out.replace(quote, "&quot;" if quote == '"' else "&apos;")
    return out.replace("\r", "&#13;").replace("\n", "&#10;").replace("\t", "&#9;")


class RawNode:
    """Text, a comment, a CDATA section or a processing instruction, kept as written (`text` includes the markup)."""

    __slots__ = ("kind", "text")

    def __init__(self, kind: str, text: str) -> None:
        self.kind = kind
        self.text = text

    @property
    def is_blank(self) -> bool:
        return self.kind == "text" and not self.text.strip(_WHITESPACE)

    def __repr__(self) -> str:
        return f"RawNode({self.kind!r}, {self.text!r})"


class _Attribute:
    __slots__ = ("lead", "name", "eq", "quote", "raw")

    def __init__(self, lead: str, name: str, eq: str, quote: str, raw: str) -> None:
        self.lead = lead  # the whitespace before the name
        self.name = name
        self.eq = eq  # from the end of the name to the opening quote: "=" or " = "
        self.quote = quote
        self.raw = raw  # the value as written, without the quotes

    def render(self) -> str:
        return f"{self.lead}{self.name}{self.eq}{self.quote}{self.raw}{self.quote}"


Node = Union["Element", RawNode]


class Element:
    def __init__(self, name: str) -> None:
        self.name = name
        self._attributes: List[_Attribute] = []
        self.tail = ""  # the whitespace between the last attribute and the closing bracket
        self.self_closing = False
        self.end_pad = ""  # the whitespace inside the end tag after the name
        self.children: List[Node] = []
        self.parent: Optional[Element] = None
        self.line: Optional[int] = None
        self.column: Optional[int] = None
        self._document: Optional[Document] = None  # set on the root only
        self._root_indent: Optional[str] = None

    def __repr__(self) -> str:
        where = f" at {self.line}:{self.column}" if self.line else ""
        return f"<Element {self.name}{where}>"

    # ---- reading ----------------------------------------------------------------------------------------------------------

    @property
    def document(self) -> Optional["Document"]:
        node = self
        while node.parent is not None:
            node = node.parent
        return node._document

    @property
    def elements(self) -> List["Element"]:
        """The child elements, in order."""
        return [c for c in self.children if isinstance(c, Element)]

    def child(self, name: str) -> Optional["Element"]:
        for c in self.children:
            if isinstance(c, Element) and c.name == name:
                return c
        return None

    def attribute_names(self) -> List[str]:
        return [a.name for a in self._attributes]

    def items(self) -> List[tuple]:
        """(name, decoded value) of every attribute, in source order."""
        return [(a.name, _decode_attribute(a.raw)) for a in self._attributes]

    def has(self, name: str) -> bool:
        return self._find_attribute(name) is not None

    def get(self, name: str, default: Optional[str] = None) -> Optional[str]:
        attribute = self._find_attribute(name)
        return default if attribute is None else _decode_attribute(attribute.raw)

    def get_raw(self, name: str) -> Optional[str]:
        """The value as it stands in the file (entities not resolved)."""
        attribute = self._find_attribute(name)
        return None if attribute is None else attribute.raw

    def iter(self) -> Iterator["Element"]:
        """This element and every descendant element in document order. A snapshot: the tree may be edited while it is walked."""
        return iter(self._collect())

    def find_all(self, predicate: Callable[["Element"], bool]) -> List["Element"]:
        return [e for e in self._collect() if predicate(e)]

    @property
    def indent(self) -> Optional[str]:
        """The whitespace between the start of this element's line and its start tag, or None when something else stands before
        it on its line (an inline element)."""
        if self.parent is None:
            return self._root_indent
        siblings = self.parent.children
        index = _index_of(siblings, self)
        if index == 0:
            return None
        previous = siblings[index - 1]
        if isinstance(previous, RawNode) and previous.kind == "text":
            match = _INDENT_AFTER_NEWLINE.search(previous.text)
            if match:
                return match.group(1)
        return None

    # ---- editing attributes -----------------------------------------------------------------------------------------------

    def set(self, name: str, value: str) -> None:
        """Set an attribute. An existing one keeps its place, its quotes and, when its value does not change, its raw text. A new
        one goes last: on the same line as the others, or on a line of its own with their indentation when the start tag spans
        lines."""
        if not isinstance(value, str):
            raise TypeError(f"attribute {name!r} of <{self.name}>: the value must be a str, not {type(value).__name__}")
        attribute = self._find_attribute(name)
        if attribute is not None:
            if _decode_attribute(attribute.raw) != value:
                attribute.raw = escape_attribute(value, attribute.quote)
            return
        lead = " "
        for existing in self._attributes:
            if "\n" in existing.lead:
                lead = existing.lead
        self._attributes.append(_Attribute(lead, name, "=", '"', escape_attribute(value)))

    def remove(self, name: str) -> bool:
        """Remove an attribute together with the whitespace before it. False when there was none."""
        attribute = self._find_attribute(name)
        if attribute is None:
            return False
        self._attributes.remove(attribute)
        return True

    def rename(self, name: str) -> None:
        self.name = name

    # ---- editing children -------------------------------------------------------------------------------------------------

    def add_child(self, fragment: str, position: Optional[int] = None, *, expand: bool = True) -> "Element":
        """Insert the element written in `fragment` (one element, any whitespace around it is dropped) as child element number
        `position` (among the child elements; None appends after the last node that is not blank). The new child takes the
        indentation of its siblings, or one level deeper than this element when it has none; a self-closing element is opened.
        A multi-line fragment is indented as a whole (its first line has no indentation of its own). With `expand` an element
        of the fragment that has child elements and no whitespace between them is laid out one child per line. Returns the
        new element."""
        elements = self.elements
        if position is None:
            position = len(elements)
        if not 0 <= position <= len(elements):
            raise IndexError(f"position {position} of {len(elements)} child elements of <{self.name}>")
        newline = self._newline()
        unit = self._unit()
        if self.self_closing:
            self.self_closing = False
            self.tail = ""
            self.end_pad = ""
            self.children = []
        reference = elements[min(position, len(elements) - 1)] if elements else None
        child_indent = reference.indent if reference is not None else None
        if reference is None:
            own = self.indent
            child_indent = None if own is None else own + unit
        base = child_indent or ""
        text = fragment.strip(_WHITESPACE).replace("\r\n", "\n").replace("\n", newline)
        child = _Parser(text.replace(newline, newline + base)).parse_fragment()
        for created in child.iter():
            created.line = created.column = None
        if expand:
            _expand(child, base, unit, newline)
        child.parent = self
        separator = [] if child_indent is None else [RawNode("text", newline + child_indent)]
        if elements:
            if position < len(elements):
                index = _index_of(self.children, elements[position])
                self.children[index:index] = [child] + separator
            else:
                index = self._last_content_index() + 1
                self.children[index:index] = separator + [child]
        elif child_indent is None:
            self.children = [child]
        else:
            kept = self.children[: self._last_content_index() + 1]
            self.children = kept + [RawNode("text", newline + child_indent), child, RawNode("text", newline + (self.indent or ""))]
        return child

    def remove_child(self, child: "Element") -> None:
        """Remove a child element together with the whitespace line before it."""
        index = _index_of(self.children, child)
        if index > 0:
            previous = self.children[index - 1]
            if isinstance(previous, RawNode) and previous.is_blank and "\n" in previous.text:
                del self.children[index - 1]
                index -= 1
        del self.children[index]
        child.parent = None

    def replace_children(self, fragments: List[str], *, expand: bool = True) -> List["Element"]:
        """Replace the whole content of this element (child elements, comments, text) by the elements written in `fragments`,
        laid out as `add_child` does. An empty list leaves a self-closing element."""
        for old in self.elements:
            old.parent = None
        self.children = []
        if not fragments:
            self.self_closing = True
            self.tail = self.tail or _DEFAULT_TAIL_OF_OPENED_EMPTY
            return []
        if self.self_closing:
            self.self_closing = False
            self.tail = ""
        return [self.add_child(f, expand=expand) for f in fragments]

    # ---- rendering --------------------------------------------------------------------------------------------------------

    def start_tag(self) -> str:
        attributes = "".join(a.render() for a in self._attributes)
        return f"<{self.name}{attributes}{self.tail}{'/>' if self.self_closing else '>'}"

    def end_tag(self) -> str:
        return "" if self.self_closing else f"</{self.name}{self.end_pad}>"

    def to_text(self) -> str:
        out: List[str] = []
        _render(self, out)
        return "".join(out)

    # ---- internals --------------------------------------------------------------------------------------------------------

    def _find_attribute(self, name: str) -> Optional[_Attribute]:
        for a in self._attributes:
            if a.name == name:
                return a
        return None

    def _collect(self) -> List["Element"]:
        out: List[Element] = []
        stack = [self]
        while stack:
            node = stack.pop()
            out.append(node)
            stack.extend(reversed(node.elements))
        return out

    def _last_content_index(self) -> int:
        """The index of the last child that is not a blank text, -1 when there is none."""
        for i in range(len(self.children) - 1, -1, -1):
            c = self.children[i]
            if not (isinstance(c, RawNode) and c.is_blank):
                return i
        return -1

    def _newline(self) -> str:
        document = self.document
        return document.newline if document is not None else _DEFAULT_NEWLINE

    def _unit(self) -> str:
        document = self.document
        return document.indent_unit() if document is not None else _DEFAULT_UNIT


def _index_of(nodes: List[Node], node: Node) -> int:
    for i, n in enumerate(nodes):
        if n is node:
            return i
    raise ValueError("the node is not a child of this element")


def _render(root: Element, out: List[str]) -> None:
    stack: List[Union[Node, str]] = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, str):
            out.append(node)
        elif isinstance(node, RawNode):
            out.append(node.text)
        else:
            out.append(node.start_tag())
            if not node.self_closing:
                stack.append(node.end_tag())
                stack.extend(reversed(node.children))


def _expand(element: Element, indent: str, unit: str, newline: str) -> None:
    """Lay out the compact elements of a new fragment one child per line; `indent` is the indentation of `element`."""
    stack = [(element, indent)]
    while stack:
        node, own = stack.pop()
        children = node.elements
        if not children:
            continue
        if not any(isinstance(c, RawNode) for c in node.children):
            inner = own + unit
            laid_out: List[Node] = []
            for c in children:
                laid_out.append(RawNode("text", newline + inner))
                laid_out.append(c)
            laid_out.append(RawNode("text", newline + own))
            node.children = laid_out
        for c in children:
            stack.append((c, _own_indent(node, c, own, unit)))


def _own_indent(parent: Element, child: Element, parent_indent: str, unit: str) -> str:
    index = _index_of(parent.children, child)
    if index > 0:
        previous = parent.children[index - 1]
        if isinstance(previous, RawNode) and previous.kind == "text":
            match = _INDENT_AFTER_NEWLINE.search(previous.text)
            if match:
                return match.group(1)
    return parent_indent + unit


class Document:
    def __init__(self, prolog: str, root: Element, epilog: str) -> None:
        self.prolog = prolog
        self.root = root
        self.epilog = epilog
        self.newline = "\r\n" if "\r\n" in prolog + root.to_text() + epilog else _DEFAULT_NEWLINE
        root._document = self
        match = _INDENT_OF_PROLOG.search(prolog[1:] if prolog.startswith(_BOM) else prolog)
        root._root_indent = match.group(1) if match else None

    def iter(self) -> Iterator[Element]:
        return self.root.iter()

    def find_all(self, predicate: Callable[[Element], bool]) -> List[Element]:
        return self.root.find_all(predicate)

    def indent_unit(self) -> str:
        """The indentation step of the file: the first line-start element found one level inside a line-start parent; two
        spaces when the file shows none."""
        for element in self.root.iter():
            parent = element.parent
            if parent is None:
                continue
            inner, outer = element.indent, parent.indent
            if inner is not None and outer is not None and len(inner) > len(outer) and inner.startswith(outer):
                return inner[len(outer):]
        return _DEFAULT_UNIT

    def to_text(self) -> str:
        return self.prolog + self.root.to_text() + self.epilog

    def to_bytes(self) -> bytes:
        return self.to_text().encode("utf-8")

    def __str__(self) -> str:
        return self.to_text()


class _Parser:
    def __init__(self, text: str) -> None:
        self.s = text
        self._newlines = [m.start() for m in re.finditer("\n", text)]

    # ---- positions and errors ---------------------------------------------------------------------------------------------

    def location(self, offset: int) -> tuple:
        line = bisect.bisect_left(self._newlines, offset) + 1
        start = self._newlines[line - 2] + 1 if line > 1 else 0
        return line, offset - start + 1

    def error(self, message: str, offset: int) -> XmlTextError:
        line, column = self.location(offset)
        return XmlTextError(message, line, column)

    # ---- the document -----------------------------------------------------------------------------------------------------

    def parse_document(self) -> Document:
        s = self.s
        pos = 1 if s.startswith(_BOM) else 0
        while True:
            pos = self._skip_whitespace(pos)
            if s.startswith("<?", pos):
                pos = self._find_end("?>", pos, 2, "a processing instruction")
            elif s.startswith("<!--", pos):
                pos = self._find_end("-->", pos, 4, "a comment")
            elif s.startswith("<!DOCTYPE", pos):
                pos = self._skip_doctype(pos)
            elif pos >= len(s):
                raise self.error("no root element", pos)
            elif s[pos] == "<" and not s.startswith("</", pos) and not s.startswith("<!", pos):
                break
            else:
                raise self.error("text or markup before the root element", pos)
        root, end = self._parse_element(pos)
        epilog = s[end:]
        tail = end
        while True:
            tail = self._skip_whitespace(tail)
            if tail >= len(s):
                break
            if s.startswith("<?", tail):
                tail = self._find_end("?>", tail, 2, "a processing instruction")
            elif s.startswith("<!--", tail):
                tail = self._find_end("-->", tail, 4, "a comment")
            else:
                raise self.error("content after the root element", tail)
        return Document(s[:pos], root, epilog)

    def parse_fragment(self) -> Element:
        s = self.s
        pos = self._skip_whitespace(0)
        if pos >= len(s) or s[pos] != "<":
            raise self.error("an element expected", pos)
        element, end = self._parse_element(pos)
        if self._skip_whitespace(end) < len(s):
            raise self.error("content after the element", end)
        return element

    # ---- elements ---------------------------------------------------------------------------------------------------------

    def _parse_element(self, pos: int) -> tuple:
        s = self.s
        root, pos = self._start_tag(pos)
        if root.self_closing:
            return root, pos
        stack = [root]
        while stack:
            top = stack[-1]
            lt = s.find("<", pos)
            if lt == -1:
                raise XmlTextError(f"the element <{top.name}> is not closed", top.line or 1, top.column or 1)
            if lt > pos:
                top.children.append(RawNode("text", s[pos:lt]))
            pos = lt
            if s.startswith("<!--", pos):
                end = self._find_end("-->", pos, 4, "a comment")
                top.children.append(RawNode("comment", s[pos:end]))
                pos = end
            elif s.startswith("<![CDATA[", pos):
                end = self._find_end("]]>", pos, 9, "a CDATA section")
                top.children.append(RawNode("cdata", s[pos:end]))
                pos = end
            elif s.startswith("<?", pos):
                end = self._find_end("?>", pos, 2, "a processing instruction")
                top.children.append(RawNode("pi", s[pos:end]))
                pos = end
            elif s.startswith("</", pos):
                match = _NAME.match(s, pos + 2)
                if not match or match.group() != top.name:
                    raise self.error(f"the end tag does not close <{top.name}> (opened at {top.line}:{top.column})", pos)
                end = match.end()
                pad_end = self._skip_whitespace(end)
                if pad_end >= len(s) or s[pad_end] != ">":
                    raise self.error("'>' expected in the end tag", pad_end)
                top.end_pad = s[end:pad_end]
                pos = pad_end + 1
                stack.pop()
            elif s.startswith("<!", pos):
                raise self.error("a declaration inside an element", pos)
            else:
                child, pos = self._start_tag(pos)
                child.parent = top
                top.children.append(child)
                if not child.self_closing:
                    stack.append(child)
        return root, pos

    def _start_tag(self, pos: int) -> tuple:
        s = self.s
        n = len(s)
        match = _NAME.match(s, pos + 1)
        if not match:
            raise self.error("an element name expected after '<'", pos + 1)
        element = Element(match.group())
        element.line, element.column = self.location(pos)
        p = match.end()
        names = set()
        while True:
            lead_start = p
            p = self._skip_whitespace(p)
            lead = s[lead_start:p]
            if p >= n:
                raise self.error(f"the start tag of <{element.name}> is not closed", pos)
            c = s[p]
            if c == ">":
                element.tail = lead
                return element, p + 1
            if c == "/":
                if not s.startswith("/>", p):
                    raise self.error("'>' expected after '/'", p + 1)
                element.tail = lead
                element.self_closing = True
                return element, p + 2
            if not lead:
                raise self.error("whitespace expected between attributes", p)
            name = _NAME.match(s, p)
            if not name:
                raise self.error("an attribute name expected", p)
            attribute_name = name.group()
            if attribute_name in names:
                raise self.error(f"the attribute {attribute_name} is repeated in <{element.name}>", p)
            names.add(attribute_name)
            p = name.end()
            eq_start = p
            p = self._skip_whitespace(p)
            if p >= n or s[p] != "=":
                raise self.error(f"'=' expected after the attribute {attribute_name}", p)
            p = self._skip_whitespace(p + 1)
            eq = s[eq_start:p]
            if p >= n or s[p] not in "\"'":
                raise self.error(f"a quoted value expected for the attribute {attribute_name}", p)
            quote = s[p]
            end = s.find(quote, p + 1)
            if end == -1:
                raise self.error(f"the value of the attribute {attribute_name} is not closed", p)
            raw = s[p + 1:end]
            if "<" in raw:
                raise self.error(f"'<' in the value of the attribute {attribute_name}", p + 1 + raw.index("<"))
            element._attributes.append(_Attribute(lead, attribute_name, eq, quote, raw))
            p = end + 1

    # ---- scanning helpers -------------------------------------------------------------------------------------------------

    def _skip_whitespace(self, pos: int) -> int:
        s = self.s
        n = len(s)
        while pos < n and s[pos] in _WHITESPACE:
            pos += 1
        return pos

    def _find_end(self, terminator: str, pos: int, skip: int, what: str) -> int:
        end = self.s.find(terminator, pos + skip)
        if end == -1:
            raise self.error(f"{what} is not closed", pos)
        return end + len(terminator)

    def _skip_doctype(self, pos: int) -> int:
        s = self.s
        p = pos + len("<!DOCTYPE")
        depth = 0
        quote = ""
        while p < len(s):
            c = s[p]
            if quote:
                if c == quote:
                    quote = ""
            elif c in "\"'":
                quote = c
            elif c == "[":
                depth += 1
            elif c == "]":
                depth -= 1
            elif c == ">" and depth <= 0:
                return p + 1
            p += 1
        raise self.error("the DOCTYPE is not closed", pos)


def parse(text: str) -> Document:
    return _Parser(text).parse_document()


def parse_bytes(data: bytes) -> Document:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        raise XmlTextError("the file is not UTF-8", data.count(b"\n", 0, e.start) + 1, 1) from None
    return parse(text)


def read(path) -> Document:
    """Parse a file; no newline translation, so the bytes written back are the bytes read."""
    with open(path, "rb") as f:
        return parse_bytes(f.read())
