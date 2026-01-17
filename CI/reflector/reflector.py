#!/usr/bin/env python3
"""
BagiEngine Reflection Code Generator

Parses C++ headers using libclang to extract types marked with reflection macros
(BE_REFLECT_CLASS, BE_REFLECT_FIELD, BE_REFLECT_ENUM) and generates reflection
metadata code using Jinja2 templates.

Usage:
    python reflector.py --input Player.hpp --output Player.gen.hpp --templates ./templates
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

try:
    from clang.cindex import Index, CursorKind, Config
except ImportError:
    print("Error: libclang not found. Install with: pip install libclang")
    sys.exit(1)

try:
    from jinja2 import Environment, FileSystemLoader
except ImportError:
    print("Error: Jinja2 not found. Install with: pip install Jinja2")
    sys.exit(1)


@dataclass
class FieldData:
    """Information about a reflected field."""
    name: str
    type_name: str
    is_pointer: bool = False
    is_reference: bool = False


@dataclass
class ClassData:
    """Information about a reflected class."""
    name: str
    qualified_name: str
    namespace: str
    fields: List[FieldData] = field(default_factory=list)


@dataclass
class EnumValue:
    """Information about an enum constant."""
    name: str
    value: Optional[int] = None


@dataclass
class EnumData:
    """Information about a reflected enum."""
    name: str
    qualified_name: str
    namespace: str
    underlying_type: str
    values: List[EnumValue] = field(default_factory=list)


def has_annotation(cursor, annotation_name: str) -> bool:
    """Check if a cursor has a specific clang::annotate attribute."""
    for child in cursor.get_children():
        if child.kind == CursorKind.ANNOTATE_ATTR:
            if child.displayname == annotation_name:
                return True
    return False


def has_msvc_marker(cursor, marker_name: str, source_content: str) -> bool:
    """Check if a cursor has a MSVC-style comment marker (/* marker */)."""
    if cursor.location.file is None:
        return False
    
    # Get the line containing the cursor
    try:
        lines = source_content.split('\n')
        line_num = cursor.location.line - 1
        if 0 <= line_num < len(lines):
            line = lines[line_num]
            # Check for comment marker pattern
            pattern = rf'/\*\s*{marker_name}\s*\*/'
            return bool(re.search(pattern, line))
    except Exception:
        pass
    return False


def is_reflected_class(cursor, source_content: str) -> bool:
    """Check if a class/struct is marked for reflection."""
    return (has_annotation(cursor, "reflect_class") or 
            has_msvc_marker(cursor, "BE_REFLECT_CLASS", source_content))


def is_reflected_field(cursor, source_content: str) -> bool:
    """Check if a field is marked for reflection."""
    return (has_annotation(cursor, "reflect_field") or 
            has_msvc_marker(cursor, "BE_REFLECT_FIELD", source_content))


def is_reflected_enum(cursor, source_content: str) -> bool:
    """Check if an enum is marked for reflection."""
    return (has_annotation(cursor, "reflect_enum") or 
            has_msvc_marker(cursor, "BE_REFLECT_ENUM", source_content))


def get_namespace(cursor) -> str:
    """Get the full namespace path for a cursor."""
    namespaces = []
    parent = cursor.semantic_parent
    while parent:
        if parent.kind == CursorKind.NAMESPACE:
            namespaces.append(parent.displayname)
        parent = parent.semantic_parent
    namespaces.reverse()
    return "::".join(namespaces)


def get_qualified_name(cursor) -> str:
    """Get the fully qualified name of a cursor."""
    namespace = get_namespace(cursor)
    if namespace:
        return f"{namespace}::{cursor.displayname}"
    return cursor.displayname


def extract_field_info(cursor) -> FieldData:
    """Extract field information from a field declaration cursor."""
    type_name = cursor.type.spelling
    is_pointer = cursor.type.kind.name == 'POINTER'
    is_reference = cursor.type.kind.name == 'LVALUEREFERENCE'
    
    return FieldData(
        name=cursor.displayname,
        type_name=type_name,
        is_pointer=is_pointer,
        is_reference=is_reference
    )


def extract_class_info(cursor, source_content: str) -> ClassData:
    """Extract class information including reflected fields."""
    class_data = ClassData(
        name=cursor.displayname,
        qualified_name=get_qualified_name(cursor),
        namespace=get_namespace(cursor)
    )
    
    for child in cursor.get_children():
        if child.kind == CursorKind.FIELD_DECL:
            if is_reflected_field(child, source_content):
                class_data.fields.append(extract_field_info(child))
    
    return class_data


def extract_enum_info(cursor) -> EnumData:
    """Extract enum information including all values."""
    # Get underlying type
    underlying_type = "int"
    if cursor.enum_type:
        underlying_type = cursor.enum_type.spelling
    
    enum_data = EnumData(
        name=cursor.displayname,
        qualified_name=get_qualified_name(cursor),
        namespace=get_namespace(cursor),
        underlying_type=underlying_type
    )
    
    for child in cursor.get_children():
        if child.kind == CursorKind.ENUM_CONSTANT_DECL:
            enum_data.values.append(EnumValue(
                name=child.displayname,
                value=child.enum_value
            ))
    
    return enum_data


def find_reflected_types(cursor, source_content: str, 
                         classes: List[ClassData], 
                         enums: List[EnumData]):
    """Recursively find all reflected classes and enums."""
    for child in cursor.get_children():
        # Skip system headers
        if child.location.file and str(child.location.file).startswith('/usr'):
            continue
            
        if child.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
            if child.is_definition() and is_reflected_class(child, source_content):
                classes.append(extract_class_info(child, source_content))
        
        elif child.kind == CursorKind.ENUM_DECL:
            if child.is_definition() and is_reflected_enum(child, source_content):
                enums.append(extract_enum_info(child))
        
        # Recurse into namespaces
        elif child.kind == CursorKind.NAMESPACE:
            find_reflected_types(child, source_content, classes, enums)


def parse_header(input_path: str, include_paths: List[str] = None) -> tuple:
    """Parse a header file and extract reflection data."""
    index = Index.create()
    
    # Read source content for MSVC marker detection
    with open(input_path, 'r', encoding='utf-8') as f:
        source_content = f.read()
    
    # Compiler arguments
    args = [
        '-x', 'c++',
        '-std=c++23',
        '-DCOMPILER_CLANG',  # Ensure we get the annotation syntax
    ]
    
    # Add include paths
    if include_paths:
        for path in include_paths:
            args.extend(['-I', path])
    
    # Parse the file
    translation_unit = index.parse(
        input_path,
        args=args,
        options=0x01  # CXTranslationUnit_DetailedPreprocessingRecord
    )
    
    # Check for parse errors
    if not translation_unit:
        raise RuntimeError(f"Failed to parse {input_path}")
    
    for diag in translation_unit.diagnostics:
        if diag.severity >= 3:  # Error or Fatal
            print(f"Parse error: {diag.spelling}", file=sys.stderr)
    
    # Extract reflected types
    classes: List[ClassData] = []
    enums: List[EnumData] = []
    find_reflected_types(translation_unit.cursor, source_content, classes, enums)
    
    return classes, enums


def generate_code(classes: List[ClassData], enums: List[EnumData], 
                  template_dir: str, input_filename: str) -> str:
    """Generate reflection code using Jinja2 templates."""
    env = Environment(
        loader=FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )
    
    template = env.get_template('reflection.gen.hpp.j2')
    
    return template.render(
        input_filename=input_filename,
        classes=classes,
        enums=enums
    )


def main():
    parser = argparse.ArgumentParser(
        description='Generate reflection code for BagiEngine'
    )
    parser.add_argument(
        '--input', '-i',
        required=True,
        help='Input header file to parse'
    )
    parser.add_argument(
        '--output', '-o',
        required=True,
        help='Output generated header file'
    )
    parser.add_argument(
        '--templates', '-t',
        required=True,
        help='Directory containing Jinja2 templates'
    )
    parser.add_argument(
        '--include', '-I',
        action='append',
        default=[],
        help='Additional include paths (can be specified multiple times)'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Enable verbose output'
    )
    
    args = parser.parse_args()
    
    # Validate inputs
    if not os.path.exists(args.input):
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)
    
    if not os.path.exists(args.templates):
        print(f"Error: Templates directory not found: {args.templates}", file=sys.stderr)
        sys.exit(1)
    
    # Parse the header
    if args.verbose:
        print(f"Parsing: {args.input}")
    
    try:
        classes, enums = parse_header(args.input, args.include)
    except Exception as e:
        print(f"Error parsing header: {e}", file=sys.stderr)
        sys.exit(1)
    
    if args.verbose:
        print(f"Found {len(classes)} reflected classes, {len(enums)} reflected enums")
        for cls in classes:
            print(f"  Class: {cls.qualified_name} ({len(cls.fields)} fields)")
        for enum in enums:
            print(f"  Enum: {enum.qualified_name} ({len(enum.values)} values)")
    
    # Generate code
    input_filename = os.path.basename(args.input)
    code = generate_code(classes, enums, args.templates, input_filename)
    
    # Write output
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    with open(args.output, 'w', encoding='utf-8') as f:
        f.write(code)
    
    if args.verbose:
        print(f"Generated: {args.output}")


if __name__ == '__main__':
    main()
