#!/usr/bin/env python3
"""
BagiEngine Reflection Code Generator

Parses C++ headers using regex patterns to extract types marked with reflection macros
(BE_CLASS, BE_REFLECT_FIELD, BE_REFLECT_ENUM) and generates reflection
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
class MethodParam:
    """Information about a method parameter."""
    name: str
    type_name: str


@dataclass
class MethodData:
    """Information about a reflected method."""
    name: str
    return_type: str
    params: List[MethodParam] = field(default_factory=list)
    is_const: bool = False
    is_virtual: bool = False
    is_override: bool = False


@dataclass
class ClassData:
    """Information about a reflected class."""
    name: str
    qualified_name: str  # For use inside namespace BECore (e.g., "TestData::Player")
    full_qualified_name: str  # For use outside namespaces (e.g., "BECore::TestData::Player")
    namespace: str
    fields: List[FieldData] = field(default_factory=list)
    methods: List[MethodData] = field(default_factory=list)
    is_factory_base: bool = False  # True if marked with BE_CLASS(Name, FACTORY_BASE)
    parent_class: Optional[str] = None  # Direct parent class name if inheriting


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


@dataclass
class DerivedClassData:
    """Information about a derived class for factory generation."""
    name: str           # Short class name (e.g., "ConsoleSink")
    short_name: str     # Name for enum value (e.g., "Console" if strip suffix)
    full_name: str      # Full qualified name (e.g., "BECore::ConsoleSink")
    source_file: str    # Source file where this class is defined
    include_path: str = ""  # Include path for the header (e.g., "BECore/Logger/ConsoleSink.h")


@dataclass
class FactoryBaseData:
    """Information about a factory base class marked with BE_CLASS(Name, FACTORY_BASE)."""
    name: str                                   # Base class name (e.g., "ILogSink")
    qualified_name: str                         # For use inside namespace
    full_qualified_name: str                    # Full qualified name
    namespace: str                              # Namespace (e.g., "BECore")
    enum_type_name: str = ""                    # Generated enum name (e.g., "LogSinkType")
    factory_name: str = ""                      # Generated factory name (e.g., "LogSinkFactory")
    derived: List[DerivedClassData] = field(default_factory=list)  # List of derived classes


def get_namespace_at_position(content: str, position: int) -> str:
    """Get the full namespace path at a given position in the content."""
    namespace_pattern = re.compile(r'namespace\s+(\w+)\s*\{')
    
    # Track namespace stack with (name, brace_depth_before_namespace)
    ns_stack = []
    brace_depth = 0
    i = 0
    
    while i < position:
        # Check for namespace declaration
        ns_match = namespace_pattern.match(content, i)
        if ns_match:
            # Save brace_depth BEFORE the namespace's opening brace
            ns_stack.append((ns_match.group(1), brace_depth))
            i = ns_match.end()
            # Count the namespace's opening brace
            brace_depth += 1
            continue
        
        if content[i] == '{':
            brace_depth += 1
        elif content[i] == '}':
            brace_depth -= 1
            # Pop namespaces that are now closed (when depth returns to pre-namespace level)
            while ns_stack and ns_stack[-1][1] >= brace_depth:
                ns_stack.pop()
        
        i += 1
    
    # Return full namespace path (e.g., "BECore::TestData")
    if not ns_stack:
        return ""
    
    return "::".join(ns[0] for ns in ns_stack)


def parse_method_params(params_str: str) -> List[MethodParam]:
    """Parse method parameter string into a list of MethodParam."""
    params = []
    if not params_str:
        return params
    
    # Split by comma, handling nested templates
    param_parts = []
    depth = 0
    current = ""
    for char in params_str:
        if char == '<':
            depth += 1
        elif char == '>':
            depth -= 1
        elif char == ',' and depth == 0:
            param_parts.append(current.strip())
            current = ""
            continue
        current += char
    if current.strip():
        param_parts.append(current.strip())
    
    for param in param_parts:
        # Parse "Type name" or "const Type& name" etc.
        param = param.strip()
        if not param:
            continue
        # Find last word as param name
        parts = param.rsplit(None, 1)
        if len(parts) == 2:
            param_type, param_name = parts
            # Handle reference/pointer attached to name
            while param_name and param_name[0] in '*&':
                param_type += param_name[0]
                param_name = param_name[1:]
            params.append(MethodParam(
                name=param_name.strip(),
                type_name=param_type.strip()
            ))
        elif len(parts) == 1:
            # No name, just type (unusual but handle it)
            params.append(MethodParam(
                name="",
                type_name=parts[0].strip()
            ))
    
    return params


def parse_header_regex(content: str) -> Tuple[List[ClassData], List[EnumData]]:
    """
    Parse header file using regex patterns to find reflected types.
    
    Looks for:
    - BE_CLASS(ClassName) inside struct/class body (new format)
    - BE_REFLECT_CLASS before struct/class (legacy format)
    - BE_REFLECT_FIELD or /* BE_REFLECT_FIELD */ before field declarations
    - BE_REFLECT_ENUM or /* BE_REFLECT_ENUM */ before enum class
    """
    classes: List[ClassData] = []
    enums: List[EnumData] = []
    
    # Pattern for finding struct/class declarations with optional inheritance
    # Captures: (1) class name, (2) inheritance clause (optional)
    struct_class_pattern = re.compile(
        r'(?:struct|class)\s+(\w+)\s*(?::\s*([^{]+))?\s*\{',
        re.MULTILINE
    )
    
    # Pattern for BE_CLASS(ClassName) or BE_CLASS(ClassName, FACTORY_BASE) macro
    # Captures: (1) class name, (2) optional second parameter (FACTORY_BASE)
    be_class_macro_pattern = re.compile(
        r'BE_CLASS\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)',
        re.MULTILINE
    )
    
    # Pattern to extract parent class from inheritance clause
    # Matches: "public BaseClass" or just "BaseClass"
    parent_class_pattern = re.compile(
        r'(?:public|protected|private)?\s*(\w+)',
        re.MULTILINE
    )
    
    # Legacy pattern for BE_REFLECT_CLASS struct/class
    # Matches: struct BE_REFLECT_CLASS Name { or /* BE_REFLECT_CLASS */ struct Name {
    legacy_class_pattern = re.compile(
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
    
    # Pattern for BE_FUNCTION methods
    # Matches: BE_FUNCTION ReturnType MethodName(params) [const] [override];
    # or: /* BE_FUNCTION */ ReturnType MethodName(params) [const] [override];
    # Captures: (1) return type, (2) method name, (3) params, (4) const/override qualifiers
    method_pattern = re.compile(
        r'(?:BE_FUNCTION\s+|/\*\s*BE_FUNCTION\s*\*/\s*)'
        r'(virtual\s+)?'                        # Optional virtual keyword
        r'([^(]+?)\s+'                          # Return type (non-greedy)
        r'(\w+)\s*'                             # Method name
        r'\(([^)]*)\)\s*'                       # Parameters
        r'(const\s*)?'                          # Optional const
        r'(override\s*)?'                       # Optional override
        r'(?:=\s*0\s*)?'                        # Optional pure virtual
        r';',                                   # Semicolon
        re.MULTILINE
    )
    
    # Find classes with BE_CLASS(ClassName) macro (new format)
    # First, find all struct/class declarations
    for struct_match in struct_class_pattern.finditer(content):
        struct_name = struct_match.group(1)
        inheritance_clause = struct_match.group(2)  # Optional inheritance clause
        class_start = struct_match.end() - 1  # Position of opening brace
        
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
        
        # Check if BE_CLASS macro is in this class body
        macro_match = be_class_macro_pattern.search(class_body)
        if not macro_match:
            continue
        
        macro_name = macro_match.group(1)
        macro_option = macro_match.group(2)  # FACTORY_BASE or None
        
        # Verify struct name matches macro argument
        if struct_name != macro_name:
            continue
        
        class_name = struct_name
        class_pos = struct_match.start()
        
        # Determine namespace
        ns = get_namespace_at_position(content, class_pos)
        full_qualified = f"{ns}::{class_name}" if ns else class_name
        
        # For use inside namespace BECore, strip the BECore:: prefix
        if ns.startswith("BECore::"):
            qualified = f"{ns[8:]}::{class_name}"
        elif ns == "BECore":
            qualified = class_name
        else:
            qualified = full_qualified
        
        # Parse parent class from inheritance clause
        parent_class = None
        if inheritance_clause:
            # Extract first parent class (handles "public Base", "Base", etc.)
            parent_match = parent_class_pattern.search(inheritance_clause.strip())
            if parent_match:
                parent_class = parent_match.group(1)
        
        # Check if this is a factory base class
        is_factory_base = macro_option and macro_option.upper() == "FACTORY_BASE"
        
        cls = ClassData(
            name=class_name,
            qualified_name=qualified,
            full_qualified_name=full_qualified,
            namespace=ns,
            is_factory_base=is_factory_base,
            parent_class=parent_class
        )
        
        # Find fields in class body
        for field_match in field_pattern.finditer(class_body):
            type_name = (field_match.group(1) or field_match.group(3) or "").strip()
            field_name = field_match.group(2) or field_match.group(4)
            
            if type_name and field_name:
                cls.fields.append(FieldData(
                    name=field_name,
                    type_name=type_name
                ))
        
        # Find methods in class body
        for method_match in method_pattern.finditer(class_body):
            is_virtual = bool(method_match.group(1))
            return_type = method_match.group(2).strip()
            method_name = method_match.group(3).strip()
            params_str = method_match.group(4).strip()
            is_const = bool(method_match.group(5))
            is_override = bool(method_match.group(6))
            
            # Parse parameters
            params = parse_method_params(params_str)
            
            if method_name:
                cls.methods.append(MethodData(
                    name=method_name,
                    return_type=return_type,
                    params=params,
                    is_const=is_const,
                    is_virtual=is_virtual,
                    is_override=is_override
                ))
        
        classes.append(cls)
    
    # Find legacy classes with BE_REFLECT_CLASS marker
    for match in legacy_class_pattern.finditer(content):
        class_name = match.group(1) or match.group(2) or match.group(3)
        if not class_name:
            continue
        
        # Check if this class was already found via BE_CLASS
        class_pos = match.start()
        ns = get_namespace_at_position(content, class_pos)
        full_qualified = f"{ns}::{class_name}" if ns else class_name
        
        # For use inside namespace BECore, strip the BECore:: prefix
        if ns.startswith("BECore::"):
            qualified = f"{ns[8:]}::{class_name}"
        elif ns == "BECore":
            qualified = class_name
        else:
            qualified = full_qualified
        
        # Skip if already found
        if any(c.full_qualified_name == full_qualified for c in classes):
            continue
        
        cls = ClassData(
            name=class_name,
            qualified_name=qualified,
            full_qualified_name=full_qualified,
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
        ns = get_namespace_at_position(content, enum_pos)
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


def scan_directory_for_headers(directory: str, extensions: List[str] = None) -> List[str]:
    """Scan a directory recursively for header files."""
    if extensions is None:
        extensions = ['.h', '.hpp', '.hxx']
    
    headers = []
    dir_path = Path(directory)
    
    if not dir_path.exists():
        return headers
    
    for ext in extensions:
        headers.extend(str(p) for p in dir_path.rglob(f'*{ext}'))
    
    return headers


def parse_headers_for_classes(file_paths: List[str], verbose: bool = False) -> List[ClassData]:
    """Parse multiple header files and collect all classes with BE_CLASS."""
    all_classes: List[ClassData] = []
    
    for file_path in file_paths:
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            classes, _ = parse_header_regex(content)
            
            # Add source file info to each class
            for cls in classes:
                # Store the source file path for reference
                cls.source_file = file_path
            
            all_classes.extend(classes)
            
            if verbose and classes:
                print(f"  Scanned {file_path}: {len(classes)} classes")
                
        except Exception as e:
            if verbose:
                print(f"  Warning: Could not read {file_path}: {e}")
    
    return all_classes


def compute_short_name(class_name: str, base_name: str) -> str:
    """
    Compute a short name for enum value based on class name and base class.
    
    Strips common suffix between class name and base name.
    
    Examples:
        ConsoleSink + ILogSink -> Console (common suffix: "Sink")
        FileSink + ILogSink -> File (common suffix: "Sink")
        MyWidget + IWidget -> My (common suffix: "Widget")
        ClearScreenWidget + IWidget -> ClearScreen (common suffix: "Widget")
    """
    short_name = class_name
    
    # Strip 'I' prefix from base class name if present
    base_suffix = base_name
    if base_suffix.startswith('I') and len(base_suffix) > 1:
        base_suffix = base_suffix[1:]  # ILogSink -> LogSink
    
    # Find the common suffix between class_name and base_suffix
    # e.g., ConsoleSink vs LogSink -> common suffix is "Sink"
    common_suffix = ""
    for i in range(1, min(len(class_name), len(base_suffix)) + 1):
        if class_name[-i:] == base_suffix[-i:]:
            common_suffix = class_name[-i:]
        else:
            break
    
    # Strip the common suffix from the class name
    if common_suffix and len(common_suffix) < len(class_name):
        short_name = class_name[:-len(common_suffix)]
    
    # If result is empty (class name was exactly the suffix), use original
    if not short_name:
        short_name = class_name
    
    return short_name


def compute_enum_type_name(base_name: str) -> str:
    """
    Compute enum type name from base class name.
    
    Strips 'I' prefix if present and adds 'Type' suffix.
    
    Examples:
        ILogSink -> LogSinkType
        IWidget -> WidgetType
        BaseClass -> BaseClassType
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]  # Strip 'I' prefix
    return f"{name}Type"


def compute_factory_name(base_name: str) -> str:
    """
    Compute factory class name from base class name.
    
    Strips 'I' prefix if present and adds 'Factory' suffix.
    
    Examples:
        ILogSink -> LogSinkFactory
        IWidget -> WidgetFactory
        BaseClass -> BaseClassFactory
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]  # Strip 'I' prefix
    return f"{name}Factory"


def compute_include_path(source_file: str, include_dirs: List[str]) -> str:
    """
    Compute the include path for a source file relative to include directories.
    
    Examples:
        /path/to/src/Modules/BECore/Logger/ConsoleSink.h
        with include_dir /path/to/src/Modules
        -> BECore/Logger/ConsoleSink.h
    """
    source_path = Path(source_file).resolve()
    
    for inc_dir in include_dirs:
        inc_path = Path(inc_dir).resolve()
        try:
            rel_path = source_path.relative_to(inc_path)
            # Convert to forward slashes for include paths
            return str(rel_path).replace('\\', '/')
        except ValueError:
            continue
    
    # Fallback: just use the filename
    return source_path.name


def build_factory_bases(main_classes: List[ClassData], 
                        scanned_classes: List[ClassData],
                        include_dirs: List[str] = None,
                        verbose: bool = False) -> List[FactoryBaseData]:
    """
    Build factory base data by matching derived classes to base classes.
    
    For each class marked with FACTORY_BASE in main_classes,
    find all classes in scanned_classes that inherit from it.
    """
    if include_dirs is None:
        include_dirs = []
    
    factory_bases: List[FactoryBaseData] = []
    
    # Find all factory base classes from the main file
    for cls in main_classes:
        if not cls.is_factory_base:
            continue
        
        factory_base = FactoryBaseData(
            name=cls.name,
            qualified_name=cls.qualified_name,
            full_qualified_name=cls.full_qualified_name,
            namespace=cls.namespace,
            enum_type_name=compute_enum_type_name(cls.name),
            factory_name=compute_factory_name(cls.name)
        )
        
        # Find all derived classes from scanned files
        for scanned_cls in scanned_classes:
            # Check if this class inherits from our factory base
            if scanned_cls.parent_class == cls.name:
                # Compute short name for enum value (strips common suffix)
                short_name = compute_short_name(scanned_cls.name, cls.name)
                source_file = getattr(scanned_cls, 'source_file', '')
                include_path = compute_include_path(source_file, include_dirs) if source_file else ''
                
                derived = DerivedClassData(
                    name=scanned_cls.name,
                    short_name=short_name,
                    full_name=scanned_cls.full_qualified_name,
                    source_file=source_file,
                    include_path=include_path
                )
                factory_base.derived.append(derived)
                
                if verbose:
                    print(f"    Found derived: {scanned_cls.name} -> {cls.name} (enum: {short_name})")
        
        if factory_base.derived:
            factory_bases.append(factory_base)
            if verbose:
                print(f"  Factory base {cls.name}: {len(factory_base.derived)} derived classes")
                print(f"    Enum type: {factory_base.enum_type_name}, Factory: {factory_base.factory_name}")
    
    return factory_bases


def generate_code(classes: List[ClassData], enums: List[EnumData], 
                  template_dir: str, input_filename: str,
                  factory_bases: List[FactoryBaseData] = None) -> str:
    """Generate reflection code using Jinja2 templates."""
    if factory_bases is None:
        factory_bases = []
    
    env = Environment(
        loader=FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )
    
    template = env.get_template('reflection.gen.hpp.j2')
    
    # Note: factory_bases is passed but will be empty since we generate
    # factory code in a separate file now
    return template.render(
        input_filename=input_filename,
        classes=classes,
        enums=enums,
        factory_bases=[]  # Don't include factory in main reflection file
    )


def generate_enum_factory_code(factory_bases: List[FactoryBaseData],
                               template_dir: str, input_filename: str) -> str:
    """Generate enum and factory code using separate template."""
    if not factory_bases:
        return ""
    
    env = Environment(
        loader=FileSystemLoader(template_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )
    
    template = env.get_template('enum_factory.gen.hpp.j2')
    
    return template.render(
        input_filename=input_filename,
        factory_bases=factory_bases
    )


def compute_enum_output_filename(base_name: str) -> str:
    """
    Compute output filename for enum factory file.
    
    Examples:
        ILogSink -> EnumLogSink.gen.hpp
        IWidget -> EnumWidget.gen.hpp
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]  # Strip 'I' prefix
    return f"Enum{name}.gen.hpp"


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
        '--output-enum', '-e',
        help='Output directory for enum factory files (optional, uses same dir as --output if not specified)'
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
        help='Additional include paths (used for computing include paths in generated code)'
    )
    parser.add_argument(
        '--scan-dirs', '-S',
        action='append',
        default=[],
        help='Directories to scan for derived classes (for factory generation)'
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
            factory_marker = " [FACTORY_BASE]" if cls.is_factory_base else ""
            parent_info = f" : {cls.parent_class}" if cls.parent_class else ""
            print(f"  Class: {cls.qualified_name}{parent_info}{factory_marker} ({len(cls.fields)} fields, {len(cls.methods)} methods)")
            for fld in cls.fields:
                print(f"    Field: {fld.name}: {fld.type_name}")
            for method in cls.methods:
                const_marker = " const" if method.is_const else ""
                override_marker = " override" if method.is_override else ""
                params_str = ", ".join(f"{p.type_name} {p.name}" for p in method.params)
                print(f"    Method: {method.return_type} {method.name}({params_str}){const_marker}{override_marker}")
        for enum in enums:
            print(f"  Enum: {enum.qualified_name} ({len(enum.values)} values)")
    
    # Scan directories for derived classes if any factory bases exist
    factory_bases: List[FactoryBaseData] = []
    has_factory_bases = any(cls.is_factory_base for cls in classes)
    
    if has_factory_bases and args.scan_dirs:
        if args.verbose:
            print(f"Scanning directories for derived classes...")
        
        # Collect all headers from scan directories
        all_headers: List[str] = []
        for scan_dir in args.scan_dirs:
            if os.path.exists(scan_dir):
                headers = scan_directory_for_headers(scan_dir)
                all_headers.extend(headers)
                if args.verbose:
                    print(f"  {scan_dir}: {len(headers)} headers")
        
        # Parse all scanned headers for classes
        scanned_classes = parse_headers_for_classes(all_headers, args.verbose)
        
        if args.verbose:
            print(f"  Total scanned classes: {len(scanned_classes)}")
        
        # Build factory bases with include directories for path computation
        factory_bases = build_factory_bases(classes, scanned_classes, args.include, args.verbose)
        
        if args.verbose:
            print(f"  Factory bases with derived classes: {len(factory_bases)}")
    
    # Generate code
    input_filename = os.path.basename(args.input)
    code = generate_code(classes, enums, args.templates, input_filename, factory_bases)
    
    # Write output
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    with open(args.output, 'w', encoding='utf-8') as f:
        f.write(code)
    
    if args.verbose:
        print(f"Generated: {args.output}")
    
    # Generate enum factory files if there are factory bases
    if factory_bases:
        enum_output_dir = args.output_enum if args.output_enum else output_dir
        if enum_output_dir:
            os.makedirs(enum_output_dir, exist_ok=True)
        
        for factory_base in factory_bases:
            enum_filename = compute_enum_output_filename(factory_base.name)
            enum_output_path = os.path.join(enum_output_dir, enum_filename) if enum_output_dir else enum_filename
            
            enum_code = generate_enum_factory_code([factory_base], args.templates, input_filename)
            
            with open(enum_output_path, 'w', encoding='utf-8') as f:
                f.write(enum_code)
            
            if args.verbose:
                print(f"Generated enum factory: {enum_output_path}")


if __name__ == '__main__':
    main()
