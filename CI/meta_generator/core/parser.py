"""
C++ Parser Module

Provides parsing of C++ headers to extract reflection metadata.
Supports two parsing modes:
1. LibclangParser: Uses libclang for accurate AST parsing
2. RegexParser: Fallback regex-based parsing (from original reflector.py)
"""

import re
from pathlib import Path
from typing import List, Tuple, Optional

from .models import (
    ClassData, FieldData, MethodData, MethodParam, 
    EnumData, EnumValue
)
from .env_setup import is_libclang_available

# Try to import clang
try:
    import clang.cindex
    from clang.cindex import CursorKind, TypeKind
    CLANG_AVAILABLE = True
except ImportError:
    CLANG_AVAILABLE = False


class RegexParser:
    """
    Regex-based C++ parser.
    
    Fallback parser that uses regex patterns to find reflection markers.
    Less accurate than libclang but works without LLVM installation.
    """
    
    # Pattern for finding struct/class declarations with optional inheritance
    STRUCT_CLASS_PATTERN = re.compile(
        r'(?:struct|class)\s+(\w+)\s*(?::\s*([^{]+))?\s*\{',
        re.MULTILINE
    )
    
    # Pattern for BE_CLASS(ClassName) or BE_CLASS(ClassName, FACTORY_BASE) macro
    BE_CLASS_MACRO_PATTERN = re.compile(
        r'BE_CLASS\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)',
        re.MULTILINE
    )
    
    # Pattern to extract parent class from inheritance clause
    PARENT_CLASS_PATTERN = re.compile(
        r'(?:public|protected|private)?\s*(\w+)',
        re.MULTILINE
    )
    
    # Pattern for BE_REFLECT_ENUM
    ENUM_PATTERN = re.compile(
        r'enum\s+class\s+BE_REFLECT_ENUM\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}|'
        r'/\*\s*BE_REFLECT_ENUM\s*\*/\s*enum\s+class\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}|'
        r'BE_REFLECT_ENUM\s+enum\s+class\s+(\w+)\s*(?::\s*(\w+))?\s*\{([^}]*)\}',
        re.MULTILINE | re.DOTALL
    )
    
    # Pattern for BE_REFLECT_FIELD
    FIELD_PATTERN = re.compile(
        r'BE_REFLECT_FIELD\s+([^;=]+?)\s+(\w+)\s*(?:=\s*[^;]+)?;|'
        r'/\*\s*BE_REFLECT_FIELD\s*\*/\s*([^;=]+?)\s+(\w+)\s*(?:=\s*[^;]+)?;',
        re.MULTILINE
    )
    
    # Pattern for BE_FUNCTION methods
    METHOD_PATTERN = re.compile(
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
    
    def __init__(self, include_dirs: List[str] = None):
        """
        Initialize regex parser.
        
        Args:
            include_dirs: Include directories (not used in regex parser, for API compatibility)
        """
        self.include_dirs = include_dirs or []
    
    def parse_file(self, path: Path) -> Tuple[List[ClassData], List[EnumData]]:
        """
        Parse a C++ header file using regex patterns.
        
        Args:
            path: Path to header file
            
        Returns:
            Tuple of (classes, enums) found in the file
        """
        try:
            with open(path, 'r', encoding='utf-8') as f:
                content = f.read()
        except (IOError, UnicodeDecodeError):
            return [], []
        
        classes = self._parse_classes(content, str(path))
        enums = self._parse_enums(content)
        
        return classes, enums
    
    def _get_namespace_at_position(self, content: str, position: int) -> str:
        """Get the full namespace path at a given position in the content."""
        namespace_pattern = re.compile(r'namespace\s+([\w:]+)\s*\{')
        
        ns_stack = []
        brace_depth = 0
        i = 0
        
        while i < position:
            ns_match = namespace_pattern.match(content, i)
            if ns_match:
                ns_name = ns_match.group(1)
                ns_stack.append((ns_name, brace_depth))
                i = ns_match.end()
                brace_depth += 1
                continue
            
            if content[i] == '{':
                brace_depth += 1
            elif content[i] == '}':
                brace_depth -= 1
                while ns_stack and ns_stack[-1][1] >= brace_depth:
                    ns_stack.pop()
            
            i += 1
        
        if not ns_stack:
            return ""
        
        return "::".join(ns[0] for ns in ns_stack)
    
    def _parse_method_params(self, params_str: str) -> List[MethodParam]:
        """Parse method parameter string into a list of MethodParam."""
        params = []
        if not params_str or not params_str.strip():
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
                params.append(MethodParam(name="", type_name=parts[0].strip()))
        
        return params
    
    def _parse_classes(self, content: str, source_file: str) -> List[ClassData]:
        """Parse classes from content."""
        classes: List[ClassData] = []
        
        for struct_match in self.STRUCT_CLASS_PATTERN.finditer(content):
            struct_name = struct_match.group(1)
            inheritance_clause = struct_match.group(2)
            class_start = struct_match.end() - 1
            
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
            macro_match = self.BE_CLASS_MACRO_PATTERN.search(class_body)
            if not macro_match:
                continue
            
            macro_name = macro_match.group(1)
            macro_option = macro_match.group(2)
            
            # Verify struct name matches macro argument
            if struct_name != macro_name:
                continue
            
            class_name = struct_name
            class_pos = struct_match.start()
            line_num = content[:class_pos].count('\n') + 1
            
            # Determine namespace
            ns = self._get_namespace_at_position(content, class_pos)
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
                parent_match = self.PARENT_CLASS_PATTERN.search(inheritance_clause.strip())
                if parent_match:
                    parent_class = parent_match.group(1)
            
            is_factory_base = macro_option and macro_option.upper() == "FACTORY_BASE"
            
            cls = ClassData(
                name=class_name,
                qualified_name=qualified,
                full_qualified_name=full_qualified,
                namespace=ns,
                is_factory_base=is_factory_base,
                parent_class=parent_class,
                source_file=source_file,
                line=line_num
            )
            
            # Find fields in class body
            for field_match in self.FIELD_PATTERN.finditer(class_body):
                type_name = (field_match.group(1) or field_match.group(3) or "").strip()
                field_name = field_match.group(2) or field_match.group(4)
                
                if type_name and field_name:
                    field_line = class_body[:field_match.start()].count('\n') + line_num
                    cls.fields.append(FieldData(
                        name=field_name,
                        type_name=type_name,
                        line=field_line
                    ))
            
            # Find methods in class body
            for method_match in self.METHOD_PATTERN.finditer(class_body):
                is_virtual = bool(method_match.group(1))
                return_type = method_match.group(2).strip()
                method_name = method_match.group(3).strip()
                params_str = method_match.group(4).strip()
                is_const = bool(method_match.group(5))
                is_override = bool(method_match.group(6))
                
                params = self._parse_method_params(params_str)
                method_line = class_body[:method_match.start()].count('\n') + line_num
                
                if method_name:
                    cls.methods.append(MethodData(
                        name=method_name,
                        return_type=return_type,
                        params=params,
                        is_const=is_const,
                        is_virtual=is_virtual,
                        is_override=is_override,
                        line=method_line
                    ))
            
            classes.append(cls)
        
        return classes
    
    def _parse_enums(self, content: str) -> List[EnumData]:
        """Parse enums from content."""
        enums: List[EnumData] = []
        
        for match in self.ENUM_PATTERN.finditer(content):
            enum_name = match.group(1) or match.group(4) or match.group(7)
            underlying = match.group(2) or match.group(5) or match.group(8) or "int"
            values_str = match.group(3) or match.group(6) or match.group(9) or ""
            
            if not enum_name:
                continue
            
            enum_pos = match.start()
            line_num = content[:enum_pos].count('\n') + 1
            ns = self._get_namespace_at_position(content, enum_pos)
            qualified = f"{ns}::{enum_name}" if ns else enum_name
            
            enum_data = EnumData(
                name=enum_name,
                qualified_name=qualified,
                namespace=ns,
                underlying_type=underlying,
                line=line_num
            )
            
            # Parse enum values
            for value_match in re.finditer(r'(\w+)\s*(?:=\s*([^,}]+))?', values_str):
                value_name = value_match.group(1)
                if value_name and value_name.strip():
                    enum_data.values.append(EnumValue(name=value_name))
            
            enums.append(enum_data)
        
        return enums


class LibclangParser:
    """
    Libclang-based C++ parser.
    
    Uses libclang for accurate AST parsing. Requires LLVM installation.
    """
    
    def __init__(self, include_dirs: List[str] = None):
        """
        Initialize libclang parser.
        
        Args:
            include_dirs: Include directories for parsing
        """
        if not CLANG_AVAILABLE:
            raise RuntimeError("libclang not available. Install 'clang' package and configure LLVM.")
        
        self.include_dirs = include_dirs or []
        self.index = clang.cindex.Index.create()
    
    def parse_file(self, path: Path) -> Tuple[List[ClassData], List[EnumData]]:
        """
        Parse a C++ header file using libclang.
        
        Args:
            path: Path to header file
            
        Returns:
            Tuple of (classes, enums) found in the file
        """
        # Build compiler arguments
        args = ['-x', 'c++', '-std=c++23']
        for inc_dir in self.include_dirs:
            args.append(f'-I{inc_dir}')
        
        try:
            # Parse the translation unit
            tu = self.index.parse(
                str(path),
                args=args,
                options=clang.cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
            )
        except Exception as e:
            # Fall back to regex parser on error
            fallback = RegexParser(self.include_dirs)
            return fallback.parse_file(path)
        
        classes: List[ClassData] = []
        enums: List[EnumData] = []
        
        # Walk the AST
        self._walk_ast(tu.cursor, path, classes, enums)
        
        return classes, enums
    
    def _walk_ast(self, cursor, source_path: Path, classes: List[ClassData], enums: List[EnumData]):
        """Recursively walk the AST to find reflected types."""
        
        # Only process nodes from the source file
        if cursor.location.file and str(cursor.location.file) != str(source_path):
            # Still recurse into children for includes
            for child in cursor.get_children():
                self._walk_ast(child, source_path, classes, enums)
            return
        
        if cursor.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
            class_data = self._parse_class(cursor, source_path)
            if class_data:
                classes.append(class_data)
        
        elif cursor.kind == CursorKind.ENUM_DECL:
            enum_data = self._parse_enum(cursor)
            if enum_data:
                enums.append(enum_data)
        
        # Recurse into children
        for child in cursor.get_children():
            self._walk_ast(child, source_path, classes, enums)
    
    def _parse_class(self, cursor, source_path: Path) -> Optional[ClassData]:
        """Parse a class/struct declaration."""
        class_name = cursor.spelling
        if not class_name:
            return None
        
        # Check for BE_CLASS macro by looking for specific patterns
        has_be_class = False
        is_factory_base = False
        
        # Read file content to check for BE_CLASS
        try:
            with open(source_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Find class body and check for BE_CLASS macro
            class_pattern = re.compile(
                rf'(?:struct|class)\s+{re.escape(class_name)}\s*[^{{]*\{{([^}}]*BE_CLASS\s*\([^)]*\)[^}}]*)\}}',
                re.DOTALL
            )
            match = class_pattern.search(content)
            if match:
                class_body = match.group(1)
                be_class_match = re.search(r'BE_CLASS\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)', class_body)
                if be_class_match and be_class_match.group(1) == class_name:
                    has_be_class = True
                    if be_class_match.group(2) and be_class_match.group(2).upper() == "FACTORY_BASE":
                        is_factory_base = True
        except Exception:
            pass
        
        if not has_be_class:
            return None
        
        # Get namespace
        ns = self._get_namespace(cursor)
        full_qualified = f"{ns}::{class_name}" if ns else class_name
        
        if ns.startswith("BECore::"):
            qualified = f"{ns[8:]}::{class_name}"
        elif ns == "BECore":
            qualified = class_name
        else:
            qualified = full_qualified
        
        # Get parent class
        parent_class = None
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_BASE_SPECIFIER:
                parent_class = child.type.spelling
                # Extract just the class name from fully qualified
                if '::' in parent_class:
                    parent_class = parent_class.split('::')[-1]
                break
        
        cls = ClassData(
            name=class_name,
            qualified_name=qualified,
            full_qualified_name=full_qualified,
            namespace=ns,
            is_factory_base=is_factory_base,
            parent_class=parent_class,
            source_file=str(source_path),
            line=cursor.location.line
        )
        
        # Parse fields and methods
        self._parse_class_members(cursor, cls, source_path)
        
        return cls
    
    def _parse_class_members(self, cursor, cls: ClassData, source_path: Path):
        """Parse class fields and methods."""
        # Read file content for annotation checking
        try:
            with open(source_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
        except Exception:
            lines = []
        
        for child in cursor.get_children():
            if child.kind == CursorKind.FIELD_DECL:
                # Check for BE_REFLECT_FIELD annotation
                if self._has_reflection_annotation(child, lines, "BE_REFLECT_FIELD"):
                    cls.fields.append(FieldData(
                        name=child.spelling,
                        type_name=child.type.spelling,
                        line=child.location.line,
                        column=child.location.column
                    ))
            
            elif child.kind == CursorKind.CXX_METHOD:
                # Check for BE_FUNCTION annotation
                if self._has_reflection_annotation(child, lines, "BE_FUNCTION"):
                    params = []
                    for arg in child.get_arguments():
                        params.append(MethodParam(
                            name=arg.spelling,
                            type_name=arg.type.spelling
                        ))
                    
                    cls.methods.append(MethodData(
                        name=child.spelling,
                        return_type=child.result_type.spelling,
                        params=params,
                        is_const=child.is_const_method(),
                        is_virtual=child.is_virtual_method(),
                        is_override=self._is_override(child),
                        line=child.location.line
                    ))
    
    def _has_reflection_annotation(self, cursor, lines: List[str], marker: str) -> bool:
        """Check if a cursor has a reflection annotation marker."""
        # Check for clang::annotate attribute
        for child in cursor.get_children():
            if child.kind == CursorKind.ANNOTATE_ATTR:
                annotation = child.spelling
                if "reflect" in annotation.lower():
                    return True
        
        # Fallback: check previous line for comment marker
        if lines and cursor.location.line > 0:
            line_idx = cursor.location.line - 1
            if line_idx < len(lines):
                current_line = lines[line_idx]
                if marker in current_line:
                    return True
                # Also check the previous line
                if line_idx > 0:
                    prev_line = lines[line_idx - 1]
                    if marker in prev_line:
                        return True
        
        return False
    
    def _is_override(self, cursor) -> bool:
        """Check if method has override specifier."""
        # Check tokens for 'override' keyword
        try:
            tokens = list(cursor.get_tokens())
            for token in tokens:
                if token.spelling == 'override':
                    return True
        except Exception:
            pass
        return False
    
    def _parse_enum(self, cursor) -> Optional[EnumData]:
        """Parse an enum declaration."""
        enum_name = cursor.spelling
        if not enum_name:
            return None
        
        # TODO: Check for BE_REFLECT_ENUM annotation
        # For now, return None - enums are parsed by regex fallback
        return None
    
    def _get_namespace(self, cursor) -> str:
        """Get full namespace path for a cursor."""
        parts = []
        parent = cursor.semantic_parent
        
        while parent and parent.kind != CursorKind.TRANSLATION_UNIT:
            if parent.kind == CursorKind.NAMESPACE:
                parts.insert(0, parent.spelling)
            parent = parent.semantic_parent
        
        return "::".join(parts)


def create_parser(include_dirs: List[str] = None, prefer_libclang: bool = True):
    """
    Create appropriate parser based on availability.
    
    Args:
        include_dirs: Include directories for parsing
        prefer_libclang: If True, prefer libclang when available
        
    Returns:
        Parser instance (LibclangParser or RegexParser)
    """
    if prefer_libclang and CLANG_AVAILABLE and is_libclang_available():
        try:
            return LibclangParser(include_dirs)
        except Exception:
            pass
    
    return RegexParser(include_dirs)
