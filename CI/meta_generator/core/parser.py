"""
C++ Parser Module

Provides parsing of C++ headers to extract reflection metadata.
Uses libclang for accurate AST parsing. LLVM installation is required.

Note: This parser extracts classes marked with BE_CLASS and their fields/methods.
Enum reflection is handled via CORE_ENUM macro, not parsed by this module.
"""

import re
from pathlib import Path
from typing import List, Tuple, Optional

from .models import (
    ClassData, FieldData, MethodData, MethodParam, 
    EnumData
)
from .env_setup import is_libclang_available

# Try to import clang
try:
    import clang.cindex
    from clang.cindex import CursorKind, TypeKind
    CLANG_AVAILABLE = True
except ImportError:
    CLANG_AVAILABLE = False


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
            raise RuntimeError(
                "LLVM/libclang is required but not available.\n"
                "The 'clang' Python package is not installed.\n"
                "Install with: pip install clang"
            )
        
        self.include_dirs = include_dirs or []
        self.index = clang.cindex.Index.create()
    
    def parse_file(self, path: Path) -> Tuple[List[ClassData], List[EnumData]]:
        """
        Parse a C++ header file using libclang.
        
        Args:
            path: Path to header file
            
        Returns:
            Tuple of (classes, enums) found in the file
            
        Raises:
            RuntimeError: If parsing fails
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
            raise RuntimeError(f"Failed to parse {path}: {e}")
        
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
        
        # Note: Enum reflection is handled via CORE_ENUM macro, not BE_REFLECT_ENUM
        # No enum parsing needed here
        
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
    
    def _get_namespace(self, cursor) -> str:
        """Get full namespace path for a cursor."""
        parts = []
        parent = cursor.semantic_parent
        
        while parent and parent.kind != CursorKind.TRANSLATION_UNIT:
            if parent.kind == CursorKind.NAMESPACE:
                parts.insert(0, parent.spelling)
            parent = parent.semantic_parent
        
        return "::".join(parts)


def create_parser(include_dirs: List[str] = None):
    """
    Create libclang parser.
    
    Args:
        include_dirs: Include directories for parsing
        
    Returns:
        LibclangParser instance
        
    Raises:
        RuntimeError: If LLVM/libclang is not available
    """
    if not CLANG_AVAILABLE:
        raise RuntimeError(
            "LLVM/libclang is required but not available.\n"
            "The 'clang' Python package is not installed.\n"
            "Install with: pip install clang"
        )
    
    if not is_libclang_available():
        raise RuntimeError(
            "LLVM/libclang is required but not configured.\n"
            "Options:\n"
            "  1. Run: python CI/meta_generator/meta_generator_gui.py (configure LLVM path)\n"
            "  2. Set environment variable: LIBCLANG_PATH=C:/Program Files/LLVM/bin\n"
            "  3. Install LLVM: https://github.com/llvm/llvm-project/releases"
        )
    
    return LibclangParser(include_dirs)
