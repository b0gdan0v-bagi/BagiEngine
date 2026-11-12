"""
C++ Parser Module

Provides parsing of C++ headers to extract reflection metadata.
Uses regex and brace-counting for fast, dependency-free parsing.

Note: This parser extracts classes marked with BE_CLASS and their fields/methods.
Enum reflection is handled via CORE_ENUM macro, not parsed by this module.
"""

import bisect
import re
from pathlib import Path
from typing import List, Tuple, Optional, Set

from .models import (
    ClassData, FieldData, MethodData, MethodParam,
    EnumData, ParseWarning
)

_RE_RAW_STRING_OPEN = re.compile(r'"([^()\\\s"]{0,16})\(')
_RE_NAMESPACE = re.compile(r'\bnamespace\s+([\w:]+)\s*\{')
_RE_CLASS_DEF = re.compile(
    r'(?<!friend\s)(?:struct|class)\s+(\w+)\s*(?::([^{;]*))?(\{)',
    re.DOTALL,
)
_RE_BE_MACRO = re.compile(r'\bBE_(CLASS|EVENT)\s*\(\s*(\w+)\s*(?:,\s*([^)]+?)\s*)?\)')
_RE_ICON_NAME = re.compile(r'[a-z0-9-]+')
_RE_BLANKS_AROUND_HYPHEN = re.compile(r'\s*-\s*')
_RE_FIELD_MARKER = re.compile(r'\bBE_REFLECT_FIELD\b')
_RE_METHOD_MARKER = re.compile(r'\bBE_FUNCTION\b')
_RE_ATTRIBUTE = re.compile(r'\[\[.*?\]\]\s*', re.DOTALL)
# A further declarator after a comma: `int a = 1, b = 2;` -> `b = 2`
_RE_NEXT_DECLARATOR = re.compile(r'\s*[*&]*\s*[A-Za-z_]\w*\s*(?:=|\{|\[|$)')
# Every reflection marker; BE_CLASS / BE_EVENT only count when called.
_RE_ANY_MARKER = re.compile(r'\b(?:BE_(?:CLASS|EVENT)(?=\s*\()|BE_REFLECT_FIELD\b|BE_FUNCTION\b)')
_RE_IDENT = re.compile(r'[A-Za-z_]\w*$')
# Pure virtual declaration: `virtual X Foo() const = 0;`
_RE_PURE_VIRTUAL = re.compile(r'\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?=\s*0\s*;')


def _blank(chunk: str) -> str:
    """Spaces of the same length, newlines kept (so offsets and line numbers survive)."""
    return ''.join(ch if ch == '\n' else ' ' for ch in chunk)


def _mask_source(raw: str) -> str:
    """Blank out comments and the contents of string and character literals.

    The result has the same length and the same newlines as *raw*, so an offset in it is an
    offset in the file and its line number is the line of the file. Quotes stay, which keeps
    `"http://x"` from being read as a comment and `"{"` from being counted as a brace.
    """
    out: List[str] = []
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        nxt = raw[i + 1] if i + 1 < n else ''
        if ch == '/' and nxt == '/':
            end = raw.find('\n', i)
            end = n if end == -1 else end
            # a `//` comment that ends in a backslash continues on the next line
            while end < n and raw[i:end].rstrip('\r').endswith('\\'):
                nl = raw.find('\n', end + 1)
                end = n if nl == -1 else nl
            out.append(_blank(raw[i:end]))
            i = end
        elif ch == '/' and nxt == '*':
            close = raw.find('*/', i + 2)
            end = n if close == -1 else close + 2
            out.append(_blank(raw[i:end]))
            i = end
        elif ch == '"':
            raw_open = _RE_RAW_STRING_OPEN.match(raw, i) if i > 0 and raw[i - 1] == 'R' else None
            if raw_open:
                terminator = ')' + raw_open.group(1) + '"'
                close = raw.find(terminator, raw_open.end())
                end = n if close == -1 else close + len(terminator)
                out.append('"' + _blank(raw[i + 1:end - 1]) + '"')
                i = end
                continue
            end = i + 1
            while end < n and raw[end] != '"' and raw[end] != '\n':
                end += 2 if raw[end] == '\\' else 1
            end = min(end, n)
            closed = end < n and raw[end] == '"'
            out.append('"' + _blank(raw[i + 1:end]) + ('"' if closed else ''))
            i = end + 1 if closed else end
        elif ch == "'":
            # `1'000'000` is a digit separator, not a character literal
            start = i
            while start > 0 and (raw[start - 1].isalnum() or raw[start - 1] in "_'"):
                start -= 1
            if start < i and raw[start].isdigit():
                out.append(ch)
                i += 1
                continue
            end = i + 1
            while end < n and raw[end] != "'" and raw[end] != '\n':
                end += 2 if raw[end] == '\\' else 1
            end = min(end, n)
            closed = end < n and raw[end] == "'"
            out.append("'" + _blank(raw[i + 1:end]) + ("'" if closed else ''))
            i = end + 1 if closed else end
        else:
            out.append(ch)
            i += 1
    return ''.join(out)


def _directive_ranges(text: str) -> List[Tuple[int, int]]:
    """Ranges of the preprocessor lines (`#define` and friends, with their `\\` continuations)."""
    ranges: List[Tuple[int, int]] = []
    pos = 0
    n = len(text)
    while pos < n:
        end = text.find('\n', pos)
        end = n if end == -1 else end
        if text[pos:end].lstrip().startswith('#'):
            start = pos
            while text[pos:end].rstrip().endswith('\\') and end < n:
                pos = end + 1
                end = text.find('\n', pos)
                end = n if end == -1 else end
            ranges.append((start, end))
        pos = end + 1
    return ranges


def _find_matching_brace(text: str, start: int) -> int:
    """Return index past the closing '}' that matches the '{' at *start*."""
    depth = 1
    pos = start + 1
    length = len(text)
    while pos < length and depth > 0:
        ch = text[pos]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
        pos += 1
    return pos


def _extract_parent_class(inheritance: str) -> Optional[str]:
    """Extract parent class name from inheritance clause like 'public IWidget, ...'."""
    if not inheritance:
        return None
    inheritance = inheritance.strip()
    first = inheritance.split(',')[0].strip()
    parts = first.split()
    if parts and parts[0] in ('public', 'private', 'protected'):
        parts = parts[1:]
    if not parts:
        return None
    parent = ' '.join(parts)
    # Handle template bases like EventBase<SimpleEvent>
    angle = parent.find('<')
    if angle != -1:
        parent = parent[:angle].strip()
    if '::' in parent:
        parent = parent.split('::')[-1]
    return parent or None


def _declaration_end(text: str, start: int, stop: int, allow_body: bool) -> Optional[Tuple[int, int]]:
    """Find where the declaration that begins at *start* ends, inside text[:stop].

    Returns (end of the declaration text, position to continue from) or None when no end is
    found. A declaration ends at the first ';' outside (), [] and {}; with *allow_body* it
    also ends at a '{' outside parentheses, the start of an inline function body, which is
    skipped up to its matching '}' (and the ';' that may follow it).
    """
    depth = 0
    pos = start
    while pos < stop:
        ch = text[pos]
        if ch in '([{':
            if ch == '{' and depth == 0 and allow_body:
                body_end = _find_matching_brace(text, pos)
                if body_end > stop:
                    return None
                after = body_end
                while after < stop and text[after].isspace():
                    after += 1
                if after < stop and text[after] == ';':
                    body_end = after + 1
                return pos, body_end
            depth += 1
        elif ch in ')]}':
            if depth == 0:
                return None
            depth -= 1
        elif ch == ';' and depth == 0:
            return pos, pos + 1
        pos += 1
    return None


def _field_head(decl: str) -> str:
    """The declarator part of a field declaration: everything before its '=' or '{' initializer."""
    angle = 0
    nesting = 0
    for i, ch in enumerate(decl):
        if ch == '<':
            angle += 1
        elif ch == '>':
            angle = max(angle - 1, 0)
        elif ch in '([':
            nesting += 1
        elif ch in ')]':
            nesting -= 1
        elif ch in '={' and angle == 0 and nesting == 0:
            return decl[:i]
    return decl


def _field_problem(decl: str) -> Optional[str]:
    """Why a field declaration cannot be reflected as one name and one type, or None."""
    head = _field_head(decl)
    angle = 0
    nesting = 0
    for i, ch in enumerate(head):
        if ch == '<':
            angle += 1
        elif ch == '>':
            angle = max(angle - 1, 0)
        elif ch in '([':
            nesting += 1
        elif ch in ')]':
            nesting -= 1
        elif ch == ',' and angle == 0 and nesting == 0:
            return "several declarators in one declaration"
        elif ch == ':' and angle == 0 and nesting == 0:
            if head[i + 1:i + 2] == ':' or head[i - 1:i] == ':':
                continue
            return "bitfield"
    # `int a = 1, b = 2;`: a comma outside every bracket of the initializer that starts a declarator
    nesting = 0
    for i in range(len(head), len(decl)):
        ch = decl[i]
        if ch in '({[':
            nesting += 1
        elif ch in ')}]':
            nesting -= 1
        elif ch == ',' and nesting == 0 and _RE_NEXT_DECLARATOR.match(decl, i + 1):
            return "several declarators in one declaration"
    return None


def _split_type_and_name(decl: str) -> Optional[Tuple[str, str]]:
    """Split a field declaration (after BE_REFLECT_FIELD, before ;) into (type, name).

    The initializer (`= ...` or `{...}`) is cut off first; the name is the last identifier
    before it. Returns None for what has no such shape (arrays, function pointers).
    """
    head = _RE_ATTRIBUTE.sub('', _field_head(decl)).strip()
    m = _RE_IDENT.search(head)
    if not m:
        return None
    name = m.group(0)
    type_name = ' '.join(head[:m.start()].split())
    if type_name.startswith('mutable '):
        type_name = type_name[len('mutable '):].strip()
    if not type_name:
        return None
    return type_name, name


# Words before a '(' that open a parenthesised part of a declaration and not its parameter list.
_NON_PARAMETER_PARENS = frozenset({'decltype', 'noexcept', 'alignas', 'sizeof', 'requires'})
_PARAM_QUALIFIERS = frozenset({'const', 'volatile', 'struct', 'class', 'enum', 'typename'})
_RE_LAST_WORD = re.compile(r'(\w+)\s*$')


def _matching_paren(text: str, start: int) -> int:
    """Index of the ')' that matches the '(' at *start*, or -1."""
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == '(':
            depth += 1
        elif text[pos] == ')':
            depth -= 1
            if depth == 0:
                return pos
    return -1


def _find_parameter_list(decl: str) -> Optional[Tuple[int, int, str]]:
    """Locate the parameter list of a method declaration: (open, close, text before the open paren).

    Skips the parentheses of `decltype(...)`, `noexcept(...)` and the like, and template
    arguments such as the `(int)` of `std::function<void(int)>`. For `operator()` the name
    owns the first pair and the parameters are the second one.
    """
    angle = 0
    pos = 0
    while pos < len(decl):
        ch = decl[pos]
        if ch == '<':
            angle += 1
        elif ch == '>':
            angle = max(angle - 1, 0)
        elif ch == '(':
            close = _matching_paren(decl, pos)
            if close == -1:
                return None
            before = decl[:pos]
            word = _RE_LAST_WORD.search(before)
            if angle > 0 or (word and word.group(1) in _NON_PARAMETER_PARENS):
                pos = close + 1
                continue
            if word and word.group(1) == 'operator' and decl[pos:close + 1].strip('() ') == '':
                nxt = decl.find('(', close + 1)
                if nxt == -1:
                    return None
                close = _matching_paren(decl, nxt)
                return (nxt, close, before + '()') if close != -1 else None
            return pos, close, before
        pos += 1
    return None


def _parse_method_decl(decl: str) -> Optional[Tuple[str, str, List[MethodParam], bool, bool, bool]]:
    """Parse a method declaration (after BE_FUNCTION, before ; or the inline body).

    Returns (return_type, name, params, is_const, is_virtual, is_override) or None.
    """
    decl = _RE_ATTRIBUTE.sub('', decl.strip())
    if re.match(r'template\b', decl):
        return None  # the generated table takes `&Class::Name`, which a template cannot give

    # Remove pure virtual '= 0'
    decl = re.sub(r'\s*=\s*0\s*$', '', decl)

    located = _find_parameter_list(decl)
    if not located:
        return None
    paren_open, paren_close, before_paren = located
    tail = decl[paren_close + 1:]
    tail = tail.split('->', 1)[0]
    is_override = bool(re.search(r'\boverride\b', tail))
    is_const = bool(re.search(r'\bconst\b', tail))

    params_str = decl[paren_open + 1:paren_close].strip()
    before_paren = before_paren.strip()

    is_virtual = bool(re.search(r'\bvirtual\b', before_paren))
    if is_virtual:
        before_paren = re.sub(r'\bvirtual\b\s*', '', before_paren).strip()

    # Split before_paren into return_type and method_name
    if before_paren.endswith('operator()'):
        method_name = 'operator()'
        return_type = before_paren[:-len('operator()')].strip()
    else:
        m = _RE_IDENT.search(before_paren)
        if not m:
            return None
        method_name = m.group(0)
        return_type = before_paren[:m.start()].strip()
    if not return_type:
        return None

    # Parse params
    params: List[MethodParam] = []
    if params_str:
        for param_str in _split_params(params_str):
            param_str = param_str.strip()
            if not param_str:
                continue
            pm = _parse_single_param(param_str)
            if pm:
                params.append(pm)

    return return_type, method_name, params, is_const, is_virtual, is_override


def _split_params(params_str: str) -> List[str]:
    """Split parameter list by commas, respecting angle brackets, parens, brackets and braces."""
    parts = []
    depth = 0
    current = []
    for ch in params_str:
        if ch in '<({[':
            depth += 1
        elif ch in '>)}]':
            depth -= 1
        elif ch == ',' and depth == 0:
            parts.append(''.join(current))
            current = []
            continue
        current.append(ch)
    if current:
        parts.append(''.join(current))
    return parts


def _strip_default_argument(param_str: str) -> str:
    """`int a = 5` -> `int a`: cut at the first '=' outside brackets."""
    depth = 0
    for i, ch in enumerate(param_str):
        if ch in '<({[':
            depth += 1
        elif ch in '>)}]':
            depth -= 1
        elif ch == '=' and depth == 0:
            return param_str[:i].strip()
    return param_str


def _parse_single_param(param_str: str) -> Optional[MethodParam]:
    """Parse a single parameter like 'const eastl::string& name' into MethodParam."""
    param_str = _strip_default_argument(param_str.strip())
    if not param_str:
        return None

    # Find last identifier (param name)
    # But handle case where there's no name (just type)
    m = _RE_IDENT.search(param_str)
    if not m:
        return MethodParam(name='', type_name=param_str)

    candidate_name = m.group(0)
    candidate_type = param_str[:m.start()].strip()

    # If type is empty or only qualifiers (`const Foo`), the whole thing is just a type
    if not candidate_type or all(word in _PARAM_QUALIFIERS for word in candidate_type.split()):
        return MethodParam(name='', type_name=param_str)

    # Handle edge case: '&' or '*' between type and name
    return MethodParam(name=candidate_name, type_name=candidate_type)


class RegexParser:
    """Regex-based C++ parser for reflection metadata extraction.

    A marker the parser saw but could not turn into metadata is not an error (the generator
    must keep working) but it is never silent either: `parse_file` leaves a `ParseWarning` for
    it in `last_warnings`.
    """

    def __init__(self, include_dirs: List[str] = None, known_enums: Set[str] = None):
        self.include_dirs = include_dirs or []
        self.known_enums = known_enums or set()
        # Warnings of the last parse_file() call, sorted by line.
        self.last_warnings: List[ParseWarning] = []
        # State of the file being parsed.
        self._line_starts: List[int] = []
        self._handled: Set[int] = set()
        self._warnings: List[ParseWarning] = []
        self._class_defs: List[Tuple[str, int, int]] = []
        self._reflected_ranges: List[Tuple[int, int]] = []

    def parse_file(self, path: Path) -> Tuple[List[ClassData], List[EnumData]]:
        try:
            with open(path, 'r', encoding='utf-8') as f:
                raw = f.read()
        except Exception as e:
            raise RuntimeError(f"Failed to read {path}: {e}")

        text = _mask_source(raw)

        self._line_starts = [0] + [i + 1 for i, ch in enumerate(text) if ch == '\n']
        self._handled = set()
        self._warnings = []
        self._class_defs = []
        self._reflected_ranges = []

        classes: List[ClassData] = []
        self._find_classes(text, path, classes)
        self._warn_unhandled_markers(text)

        unique = {(w.line, w.kind, w.message): w for w in self._warnings}
        self.last_warnings = sorted(unique.values(), key=lambda w: (w.line, w.kind, w.message))
        return classes, []

    # ------------------------------------------------------------------

    def _line_at(self, pos: int) -> int:
        return bisect.bisect_right(self._line_starts, pos)

    def _warn(self, pos: int, kind: str, message: str):
        self._warnings.append(ParseWarning(line=self._line_at(pos), kind=kind, message=message))

    def _find_classes(self, text: str, source_path: Path, classes: List[ClassData]):
        """Find all class/struct definitions with BE_CLASS/BE_EVENT in *text*."""
        for m in _RE_CLASS_DEF.finditer(text):
            class_name = m.group(1)
            inheritance = m.group(2)
            brace_pos = m.start(3)

            body_end = _find_matching_brace(text, brace_pos)
            body = text[brace_pos + 1:body_end - 1]
            self._class_defs.append((class_name, brace_pos + 1, body_end - 1))

            be = _RE_BE_MACRO.search(body)
            if not be or be.group(2) != class_name:
                continue
            self._handled.add(brace_pos + 1 + be.start())
            self._reflected_ranges.append((brace_pos + 1, body_end - 1))

            is_event = (be.group(1) == "EVENT")
            options_str = be.group(3) or ""
            options = [o.strip() for o in options_str.split(",") if o.strip()]
            is_factory_base = any(o.upper() == "FACTORY_BASE" for o in options)
            register_self = any(o.upper() == "REGISTER_SELF" for o in options)
            element_name = None
            icon = ""
            for opt in options:
                if "=" in opt:
                    key, val = opt.split("=", 1)
                    if key.strip().upper() == "ELEMENT":
                        element_name = val.strip()
                    elif key.strip().upper() == "ICON":
                        icon = self._read_icon(val, brace_pos + 1 + be.start(), class_name)

            parent_class = _extract_parent_class(inheritance)
            ns = self._namespace_at(text, m.start())
            full_qualified = f"{ns}::{class_name}" if ns else class_name

            if ns.startswith("BECore::"):
                qualified = f"{ns[8:]}::{class_name}"
            elif ns == "BECore":
                qualified = class_name
            else:
                qualified = full_qualified

            line_no = text[:m.start()].count('\n') + 1

            cls = ClassData(
                name=class_name,
                qualified_name=qualified,
                full_qualified_name=full_qualified,
                namespace=ns,
                is_factory_base=is_factory_base,
                is_event=is_event,
                is_abstract=bool(_RE_PURE_VIRTUAL.search(body)),
                parent_class=parent_class,
                source_file=str(source_path),
                line=line_no,
                element_name=element_name,
                register_self=register_self,
                icon=icon,
            )

            self._warn_nested_markers(body, brace_pos + 1, class_name)
            self._parse_fields(body, cls, brace_pos + 1)
            self._parse_methods(body, cls, brace_pos + 1)
            classes.append(cls)

    def _read_icon(self, value: str, pos: int, class_name: str) -> str:
        """The Lucide name of ICON=<name>; a value that is not a plain name warns and is ignored.

        clang-format reads `ICON=layout-dashboard` as an expression and writes `ICON = layout - dashboard`, so
        the blanks around a hyphen are not part of the name.
        """
        value = _RE_BLANKS_AROUND_HYPHEN.sub("-", value.strip())
        if _RE_ICON_NAME.fullmatch(value):
            return value
        self._warn(
            pos, "class",
            f"ICON={value} ignored on '{class_name}': the value must be a Lucide name "
            f"in kebab case ([a-z0-9-]+)")
        return ""

    def _namespace_at(self, text: str, pos: int) -> str:
        """Determine the namespace at a given character position via brace counting."""
        # One entry per open namespace brace: `namespace A::B {` is one brace and two segments.
        ns_stack: List[List[str]] = []
        # Track opening braces that are NOT namespace braces
        generic_depth = 0
        i = 0
        while i < pos:
            ns_m = _RE_NAMESPACE.match(text, i)
            if ns_m and ns_m.start() == i:
                # Namespace declaration — push its segments as one group
                ns_stack.append(ns_m.group(1).split('::'))
                i = ns_m.end()
                continue

            ch = text[i]
            if ch == '{':
                # Non-namespace brace
                generic_depth += 1
            elif ch == '}':
                if generic_depth > 0:
                    generic_depth -= 1
                elif ns_stack:
                    ns_stack.pop()
            i += 1

        return '::'.join(seg for group in ns_stack for seg in group)

    def _warn_nested_markers(self, body: str, base: int, class_name: str):
        """Warn about field and method markers that sit in a class nested in the reflected one.

        The field and method regexes run over the whole body, so such a marker would be
        attributed to the outer class.
        """
        for nested in _RE_CLASS_DEF.finditer(body):
            start = nested.start(3)
            end = _find_matching_brace(body, start)
            for marker in _RE_ANY_MARKER.finditer(body, start, end):
                if marker.group(0) in ('BE_REFLECT_FIELD', 'BE_FUNCTION'):
                    kind = 'field' if marker.group(0) == 'BE_REFLECT_FIELD' else 'method'
                    self._warn(
                        base + marker.start(), kind,
                        f"{marker.group(0)} sits in the nested class '{nested.group(1)}' and is "
                        f"attributed to '{class_name}'; reflect nested classes separately")

    def _swallowed(self, raw_decl: str, decl_pos: int, marker_pos: int, kind: str, marker: str) -> bool:
        """Warn when the text of a declaration runs over another marker (a missing ';').

        The swallowed markers count as handled, so they are not reported a second time.
        """
        inner = list(_RE_ANY_MARKER.finditer(raw_decl))
        if not inner:
            return False
        for other in inner:
            self._handled.add(decl_pos + other.start())
        self._warn(
            marker_pos, kind,
            f"{marker} declaration runs over the next marker '{inner[0].group(0)}' "
            f"(no ';' ends it)")
        return True

    def _parse_fields(self, body: str, cls: ClassData, base: int = 0):
        pos = 0
        for m in _RE_FIELD_MARKER.finditer(body):
            if m.start() < pos:
                continue  # inside the declaration of the previous marker
            found = _declaration_end(body, m.end(), len(body), allow_body=False)
            if not found:
                continue  # no ';': the marker stays unhandled and is reported as such
            decl_end, pos = found
            full = body[m.end():decl_end]
            raw = full.strip()
            decl_pos = base + m.end() + len(full) - len(full.lstrip())
            marker_pos = base + m.start()
            self._handled.add(marker_pos)
            if self._swallowed(raw, decl_pos, marker_pos, 'field', 'BE_REFLECT_FIELD'):
                continue
            shown = ' '.join(raw.split())
            problem = _field_problem(raw)
            if problem:
                self._warn(
                    marker_pos, 'field',
                    f"BE_REFLECT_FIELD '{shown}' skipped: {problem} (declare one field per marker)")
                continue
            parsed = _split_type_and_name(raw)
            if not parsed:
                self._warn(
                    marker_pos, 'field',
                    f"BE_REFLECT_FIELD '{shown}' skipped: no type and name could be "
                    f"split off (arrays, bitfields and function pointers are not supported)")
                continue
            type_name, field_name = parsed

            # Check if enum
            bare_type = type_name.split('::')[-1].strip()
            is_enum = bare_type in self.known_enums

            line_no = self._line_at(marker_pos)

            cls.fields.append(FieldData(
                name=field_name,
                type_name=type_name,
                line=line_no,
                column=0,
                is_enum=is_enum,
            ))

    def _parse_methods(self, body: str, cls: ClassData, base: int = 0):
        pos = 0
        for m in _RE_METHOD_MARKER.finditer(body):
            if m.start() < pos:
                continue  # inside the declaration or inline body of the previous marker
            found = _declaration_end(body, m.end(), len(body), allow_body=True)
            if not found:
                continue
            decl_end, pos = found
            full = body[m.end():decl_end]
            raw = full.strip()
            decl_pos = base + m.end() + len(full) - len(full.lstrip())
            marker_pos = base + m.start()
            self._handled.add(marker_pos)
            if self._swallowed(raw, decl_pos, marker_pos, 'method', 'BE_FUNCTION'):
                continue
            parsed = _parse_method_decl(raw)
            if not parsed:
                self._warn(
                    marker_pos, 'method',
                    f"BE_FUNCTION '{' '.join(raw.split())}' skipped: not a method declaration "
                    f"the parser understands")
                continue
            return_type, name, params, is_const, is_virtual, is_override = parsed
            line_no = self._line_at(marker_pos)

            cls.methods.append(MethodData(
                name=name,
                return_type=return_type,
                params=params,
                is_const=is_const,
                is_virtual=is_virtual,
                is_override=is_override,
                line=line_no,
            ))

    def _warn_unhandled_markers(self, text: str):
        """Warn about every marker that no class, field or method claimed."""
        directives = _directive_ranges(text)
        starts = [d[0] for d in directives]

        def in_directive(pos: int) -> bool:
            idx = bisect.bisect_right(starts, pos) - 1
            return idx >= 0 and pos < directives[idx][1]

        def enclosing_class(pos: int) -> Optional[str]:
            best: Optional[Tuple[str, int, int]] = None
            for entry in self._class_defs:
                if entry[1] <= pos < entry[2] and (best is None or entry[1] > best[1]):
                    best = entry
            return best[0] if best else None

        for marker in _RE_ANY_MARKER.finditer(text):
            pos = marker.start()
            if pos in self._handled or in_directive(pos):
                continue
            name = marker.group(0)
            if name in ('BE_CLASS', 'BE_EVENT'):
                called = _RE_BE_MACRO.match(text, pos)
                shown = f"{name}({called.group(2)})" if called else name
                enclosing = enclosing_class(pos)
                if enclosing is not None:
                    self._warn(
                        pos, 'class',
                        f"{shown} ignored: it is not the first marker of '{enclosing}' or names "
                        f"another class than the enclosing '{enclosing}'")
                else:
                    self._warn(
                        pos, 'class',
                        f"{shown} ignored: no class or struct definition the parser recognizes "
                        f"encloses it ('final', an export macro, alignas, an attribute before the name "
                        f"or a template specialization are not supported)")
                continue
            kind = 'field' if name == 'BE_REFLECT_FIELD' else 'method'
            if any(start <= pos < end for start, end in self._reflected_ranges):
                self._warn(pos, kind, f"{name} ignored: no ';' ends its declaration")
            else:
                enclosing = enclosing_class(pos)
                where = (f"inside '{enclosing}', which is not reflected" if enclosing is not None
                         else "outside every class the parser recognizes")
                self._warn(pos, kind, f"{name} ignored: it is {where}")


def create_parser(include_dirs: List[str] = None, known_enums: Set[str] = None):
    """Create a regex-based parser.

    Args:
        include_dirs: Include directories (kept for interface compat)
        known_enums: Set of enum type names collected via CORE_ENUM pre-scan

    Returns:
        RegexParser instance
    """
    return RegexParser(include_dirs, known_enums)
