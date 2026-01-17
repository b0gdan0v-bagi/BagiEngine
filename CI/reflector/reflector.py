#!/usr/bin/env python3
"""
BagiEngine Reflection Code Generator

Parses C++ headers using regex patterns to extract types marked with reflection macros
(BE_REFLECT_CLASS, BE_REFLECT_FIELD, BE_REFLECT_ENUM) and generates reflection
metadata code using Jinja2 templates.

This is a simplified regex-based approach that works without requiring full
C++ parsing or all include paths.

Usage:
    python reflector.py --input Player.hpp --output Player.gen.hpp --templates ./templates
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Tuple

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


@dataclass
class EnumData:
    """Information about a reflected enum."""
    name: str
    qualified_name: str
    namespace: str
    underlying_type: str
    values: List[EnumValue] = field(default_factory=list)


def parse_header_regex(content: str) -> Tuple[List[ClassData], List[EnumData]]:
    """
    Parse header file using regex patterns to find reflected types.
    
    Looks for:
    - BE_REFLECT_CLASS or /* BE_REFLECT_CLASS */ before struct/class
    - BE_REFLECT_FIELD or /* BE_REFLECT_FIELD */ before field declarations
    - BE_REFLECT_ENUM or /* BE_REFLECT_ENUM */ before enum class
    """
    classes: List[ClassData] = []
    enums: List[EnumData] = []
    
    # Track current namespace context
    current_namespace = ""
    
    # Find namespace declarations
    namespace_pattern = re.compile(r'namespace\s+(\w+)\s*\{')
    
    # Pattern for BE_REFLECT_CLASS struct/class
    # Matches: struct BE_REFLECT_CLASS Name { or /* BE_REFLECT_CLASS */ struct Name {
    class_pattern = re.compile(
        r'(?:struct|class)\s+BE_REFLECT_CLASS\s+(\w+)|'
        r'/\*\s*BE_REFLECT_CLASS\s*\*/\s*(?:struct|class)\s+(\w+)|'
        r'BE_REFLECT_CLASS\s+(?:struct|class)\s+(\w+)',
        re.MULTILINE
    )
    
    # Pattern for BE_REFLECT_ENUM
    enum_pattern = re.compile(
        r'enum\s+class\s+BE_REFLECT_ENUM\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}|'
        r'/\*\s*BE_REFLECT_ENUM\s*\*/\s*enum\s+class\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}|'
        r'BE_REFLECT_ENUM\s+enum\s+class\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}',
        re.MULTILINE | re.DOTALL
    )
    
    # Pattern for BE_REFLECT_FIELD
    field_pattern = re.compile(
        r'BE_REFLECT_FIELD\s+([^;=]+?)\s+(\w+)\s*(?:=\s*[^;]+)?;|'
        r'/\*\s*BE_REFLECT_FIELD\s*\*/\s*([^;=]+?)\s+(\w+)\s*(?:=\s*[^;]+)?;',
        re.MULTILINE
    )
    
    # Find namespaces (simplified - assumes single namespace level)
    namespace_matches = list(namespace_pattern.finditer(content))
    if namespace_matches:
        # Use the last namespace before TestData or the first one
        for match in namespace_matches:
            ns = match.group(1)
            if ns == "TestData":
                current_namespace = ns
                break
            current_namespace = ns
    
    # Find reflected classes/structs
    for match in class_pattern.finditer(content):
        class_name = match.group(1) or match.group(2) or match.group(3)
        if not class_name:
            continue
        
        # Determine namespace for this class
        # Look for namespace declaration before this class
        class_pos = match.start()
        ns = ""
        for ns_match in namespace_pattern.finditer(content[:class_pos]):
            ns = ns_match.group(1)
        
        qualified = f"{ns}::{class_name}" if ns else class_name
        
        cls = ClassData(
            name=class_name,
            qualified_name=qualified,
            namespace=ns
        )
        
        # Find the class body (from { to matching })
        class_start = content.find('{', match.end())
        if class_start == -1:
            continue
        
        # Find matching closing brace
        brace_count = 1
        class_end = class_start + 1
        while class_end < len(content) and brace_count > 0:
            if content[class_end] == '{':
                brace_count += 1
            elif content[class_end] == '}':
                brace_count -= 1
            class_end += 1
        
        class_body = content[class_start:class_end]
        
        # Find fields in class body
        for field_match in field_pattern.finditer(class_body):
            type_name = (field_match.group(1) or field_match.group(3) or "").strip()
            field_name = field_match.group(2) or field_match.group(4)
            
            if type_name and field_name:
                cls.fields.append(FieldData(
                    name=field_name,
                    type_name=type_name
                ))
        
        classes.append(cls)
    
    # Find reflected enums
    for match in enum_pattern.finditer(content):
        enum_name = match.group(1) or match.group(4) or match.group(7)
        underlying = match.group(2) or match.group(5) or match.group(8) or "int"
        values_str = match.group(3) or match.group(6) or match.group(9) or ""
        
        if not enum_name:
            continue
        
        # Determine namespace
        enum_pos = match.start()
        ns = ""
        for ns_match in namespace_pattern.finditer(content[:enum_pos]):
            ns = ns_match.group(1)
        
        qualified = f"{ns}::{enum_name}" if ns else enum_name
        
        enum_data = EnumData(
            name=enum_name,
            qualified_name=qualified,
            namespace=ns,
            underlying_type=underlying
        )
        
        # Parse enum values
        for value_match in re.finditer(r'(\w+)\s*(?:=\s*[^,}]+)?', values_str):
            value_name = value_match.group(1)
            if value_name and value_name not in ('', ' '):
                enum_data.values.append(EnumValue(name=value_name))
        
        enums.append(enum_data)
    
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
        help='Additional include paths (ignored in regex mode, kept for compatibility)'
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
    
    # Read source file
    if args.verbose:
        print(f"Parsing: {args.input}")
    
    try:
        with open(args.input, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)
    
    # Parse using regex
    classes, enums = parse_header_regex(content)
    
    if args.verbose:
        print(f"Found {len(classes)} reflected classes, {len(enums)} reflected enums")
        for cls in classes:
            print(f"  Class: {cls.qualified_name} ({len(cls.fields)} fields)")
            for fld in cls.fields:
                print(f"    - {fld.name}: {fld.type_name}")
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
