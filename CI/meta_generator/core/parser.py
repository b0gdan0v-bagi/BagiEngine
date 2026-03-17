"""
C++ Parser Module

Provides parsing of C++ headers to extract reflection metadata.
Uses regex and brace-counting for fast, dependency-free parsing.

Note: This parser extracts classes marked with BE_CLASS and their fields/methods.
Enum reflection is handled via CORE_ENUM macro, not parsed by this module.
"""

import re
from pathlib import Path
from typing import List, Tuple, Optional, Set

from .models import (
    ClassData, FieldData, MethodData, MethodParam,
    EnumData
)

_RE_LINE_COMMENT = re.compile(r'//.*?$', re.MULTILINE)
_RE_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.DOTALL)

_RE_NAMESPACE = re.compile(r'\bnamespace\s+([\w:]+)\s*\{')
_RE_CLASS_DEF = re.compile(
    r'(?<!friend\s)(?:struct|class)\s+(\w+)\s*(?::([^{;]*))?(\{)',
    re.DOTALL,
)
_RE_BE_MACRO = re.compile(r'BE_(CLASS|EVENT)\s*\(\s*(\w+)\s*(?:,\s*([^)]+?)\s*)?\)')
_RE_FIELD = re.compile(
    r'BE_REFLECT_FIELD\b\s+(.*?)\s*;',
    re.DOTALL,
)
_RE_METHOD = re.compile(
    r'BE_FUNCTION\b\s+(.*?)\s*;',
    re.DOTALL,
)
_RE_IDENT = re.compile(r'[A-Za-z_]\w*$')


def _strip_comments(text: str) -> str:
    text = _RE_BLOCK_COMMENT.sub('', text)
    text = _RE_LINE_COMMENT.sub('', text)
    return text


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


def _split_type_and_name(decl: str) -> Optional[Tuple[str, str]]:
    """Split a field declaration (after BE_REFLECT_FIELD, before ;) into (type, name).

    Handles brace and = initialisers, template angle brackets, etc.
    """
    decl = decl.strip()

    # Strip brace initialiser  e.g. {20, 20, 100, 255}
    # Find the last balanced {} block
    result = []
    depth = 0
    brace_start = -1
    for i, ch in enumerate(decl):
        if ch == '{':
            if depth == 0:
                brace_start = i
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                continue
        if depth == 0 and ch != '}':
            result.append(ch)
    decl = ''.join(result).strip()

    # Strip = initialiser
    # Walk from the end to find '=' that isn't inside angle brackets
    angle = 0
    eq_pos = -1
    for i in range(len(decl) - 1, -1, -1):
        ch = decl[i]
        if ch == '>':
            angle += 1
        elif ch == '<':
            angle -= 1
        elif ch == '=' and angle == 0:
            eq_pos = i
            break
    if eq_pos != -1:
        decl = decl[:eq_pos].strip()

    # Now split into type and name
    # The field name is the last C++ identifier token
    # Walk backwards past the identifier
    decl = decl.rstrip()
    m = _RE_IDENT.search(decl)
    if not m:
        return None
    name = m.group(0)
    type_name = decl[:m.start()].strip()
    if not type_name:
        return None
    return type_name, name


def _parse_method_decl(decl: str) -> Optional[Tuple[str, str, List[MethodParam], bool, bool, bool]]:
    """Parse a method declaration (after BE_FUNCTION, before ;).

    Returns (return_type, name, params, is_const, is_virtual, is_override) or None.
    """
    decl = decl.strip()

    # Remove pure virtual '= 0'
    decl = re.sub(r'\s*=\s*0\s*$', '', decl)

    is_override = False
    if re.search(r'\boverride\b', decl):
        is_override = True
        decl = re.sub(r'\boverride\b', '', decl).strip()

    is_const = False
    # const after closing paren
    paren_close = decl.rfind(')')
    if paren_close != -1:
        after_paren = decl[paren_close + 1:].strip()
        if 'const' in after_paren:
            is_const = True
        decl = decl[:paren_close + 1]

    is_virtual = False
    if decl.startswith('virtual '):
        is_virtual = True
        decl = decl[len('virtual '):].strip()

    # Find the parameter list
    paren_open = decl.find('(')
    paren_close = decl.rfind(')')
    if paren_open == -1 or paren_close == -1:
        return None

    params_str = decl[paren_open + 1:paren_close].strip()
    before_paren = decl[:paren_open].strip()

    # Split before_paren into return_type and method_name
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
    """Split parameter list by commas, respecting angle brackets and parens."""
    parts = []
    depth = 0
    current = []
    for ch in params_str:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        elif ch == ',' and depth == 0:
            parts.append(''.join(current))
            current = []
            continue
        current.append(ch)
    if current:
        parts.append(''.join(current))
    return parts


def _parse_single_param(param_str: str) -> Optional[MethodParam]:
    """Parse a single parameter like 'const eastl::string& name' into MethodParam."""
    param_str = param_str.strip()
    if not param_str:
        return None

    # Find last identifier (param name)
    # But handle case where there's no name (just type)
    m = _RE_IDENT.search(param_str)
    if not m:
        return MethodParam(name='', type_name=param_str)

    candidate_name = m.group(0)
    candidate_type = param_str[:m.start()].strip()

    # If type is empty, the whole thing is just a type (no param name)
    if not candidate_type:
        return MethodParam(name='', type_name=param_str)

    # Handle edge case: '&' or '*' between type and name
    return MethodParam(name=candidate_name, type_name=candidate_type)


class RegexParser:
    """Regex-based C++ parser for reflection metadata extraction."""

    def __init__(self, include_dirs: List[str] = None, known_enums: Set[str] = None):
        self.include_dirs = include_dirs or []
        self.known_enums = known_enums or set()

    def parse_file(self, path: Path) -> Tuple[List[ClassData], List[EnumData]]:
        try:
            with open(path, 'r', encoding='utf-8') as f:
                raw = f.read()
        except Exception as e:
            raise RuntimeError(f"Failed to read {path}: {e}")

        text = _strip_comments(raw)

        classes: List[ClassData] = []
        self._find_classes(text, path, classes)
        return classes, []

    # ------------------------------------------------------------------

    def _find_classes(self, text: str, source_path: Path, classes: List[ClassData]):
        """Find all class/struct definitions with BE_CLASS/BE_EVENT in *text*."""
        for m in _RE_CLASS_DEF.finditer(text):
            class_name = m.group(1)
            inheritance = m.group(2)
            brace_pos = m.start(3)

            body_end = _find_matching_brace(text, brace_pos)
            body = text[brace_pos + 1:body_end - 1]

            be = _RE_BE_MACRO.search(body)
            if not be or be.group(2) != class_name:
                continue

            is_event = (be.group(1) == "EVENT")
            options_str = be.group(3) or ""
            options = [o.strip() for o in options_str.split(",") if o.strip()]
            is_factory_base = any(o.upper() == "FACTORY_BASE" for o in options)
            element_name = None
            for opt in options:
                if "=" in opt:
                    key, val = opt.split("=", 1)
                    if key.strip().upper() == "ELEMENT":
                        element_name = val.strip()

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
                parent_class=parent_class,
                source_file=str(source_path),
                line=line_no,
                element_name=element_name,
            )

            self._parse_fields(body, cls)
            self._parse_methods(body, cls)
            classes.append(cls)

    def _namespace_at(self, text: str, pos: int) -> str:
        """Determine the namespace at a given character position via brace counting."""
        ns_stack: List[str] = []
        # Track opening braces that are NOT namespace braces
        generic_depth = 0
        i = 0
        while i < pos:
            ns_m = _RE_NAMESPACE.match(text, i)
            if ns_m and ns_m.start() == i:
                # Namespace declaration — push segments
                ns_name = ns_m.group(1)
                for seg in ns_name.split('::'):
                    ns_stack.append(seg)
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

        return '::'.join(ns_stack)

    def _parse_fields(self, body: str, cls: ClassData):
        for m in _RE_FIELD.finditer(body):
            raw = m.group(1)
            parsed = _split_type_and_name(raw)
            if not parsed:
                continue
            type_name, field_name = parsed

            # Check if enum
            bare_type = type_name.split('::')[-1].strip()
            is_enum = bare_type in self.known_enums

            line_no = body[:m.start()].count('\n') + 1

            cls.fields.append(FieldData(
                name=field_name,
                type_name=type_name,
                line=cls.line + line_no,
                column=0,
                is_enum=is_enum,
            ))

    def _parse_methods(self, body: str, cls: ClassData):
        for m in _RE_METHOD.finditer(body):
            raw = m.group(1)
            parsed = _parse_method_decl(raw)
            if not parsed:
                continue
            return_type, name, params, is_const, is_virtual, is_override = parsed
            line_no = body[:m.start()].count('\n') + 1

            cls.methods.append(MethodData(
                name=name,
                return_type=return_type,
                params=params,
                is_const=is_const,
                is_virtual=is_virtual,
                is_override=is_override,
                line=cls.line + line_no,
            ))


def create_parser(include_dirs: List[str] = None, known_enums: Set[str] = None):
    """Create a regex-based parser.

    Args:
        include_dirs: Include directories (kept for interface compat)
        known_enums: Set of enum type names collected via CORE_ENUM pre-scan

    Returns:
        RegexParser instance
    """
    return RegexParser(include_dirs, known_enums)
