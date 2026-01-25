"""
Data Models for Meta-Generator

Defines dataclasses for representing C++ reflection metadata:
- FieldData: Reflected field information
- MethodData: Reflected method information  
- ClassData: Reflected class/struct information
- EnumData: Reflected enum information
- FileMetadata: Per-file metadata with hash for caching
"""

from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any


@dataclass
class MethodParam:
    """Information about a method parameter."""
    name: str
    type_name: str


# Primitive types for serialization (attributes vs child elements)
PRIMITIVE_TYPES = frozenset({
    # Boolean
    'bool',
    # Signed integers
    'int8_t', 'int16_t', 'int32_t', 'int64_t',
    'signed char', 'short', 'int', 'long', 'long long',
    # Unsigned integers
    'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
    'unsigned char', 'unsigned short', 'unsigned int', 'unsigned long', 'unsigned long long',
    # Floating point
    'float', 'double',
    # Strings
    'eastl::string', 'std::string', 'PoolString',
    'eastl::basic_string<char>', 'std::basic_string<char>',
    # Character
    'char', 'wchar_t', 'char8_t', 'char16_t', 'char32_t',
})

# Patterns that indicate primitive types
PRIMITIVE_PATTERNS = [
    'eastl::basic_string',
    'std::basic_string',
    'PoolString',
]


def is_primitive_type(type_name: str) -> bool:
    """Check if a type is a primitive that should be serialized as attribute."""
    # Remove const, volatile, reference, pointer modifiers
    clean_type = type_name.replace('const ', '').replace('volatile ', '')
    clean_type = clean_type.rstrip('&*').strip()
    
    # Check direct match
    if clean_type in PRIMITIVE_TYPES:
        return True
    
    # Check pattern matches (for templated types like eastl::basic_string<char>)
    for pattern in PRIMITIVE_PATTERNS:
        if clean_type.startswith(pattern):
            return True
    
    return False


@dataclass
class FieldData:
    """Information about a reflected field."""
    name: str
    type_name: str
    line: int = 0
    column: int = 0
    is_primitive: bool = False  # Set by parser, used by template for SerializeAttribute vs BeginObject
    
    def __post_init__(self):
        """Automatically determine if type is primitive after initialization."""
        if not self.is_primitive:
            self.is_primitive = is_primitive_type(self.type_name)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "type_name": self.type_name,
            "line": self.line,
            "column": self.column,
            "is_primitive": self.is_primitive,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FieldData':
        """Create from dictionary."""
        return cls(
            name=data["name"],
            type_name=data["type_name"],
            line=data.get("line", 0),
            column=data.get("column", 0),
            is_primitive=data.get("is_primitive", False),
        )


@dataclass
class MethodData:
    """Information about a reflected method."""
    name: str
    return_type: str
    params: List[MethodParam] = field(default_factory=list)
    is_const: bool = False
    is_virtual: bool = False
    is_override: bool = False
    line: int = 0
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "return_type": self.return_type,
            "params": [{"name": p.name, "type_name": p.type_name} for p in self.params],
            "is_const": self.is_const,
            "is_virtual": self.is_virtual,
            "is_override": self.is_override,
            "line": self.line,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'MethodData':
        """Create from dictionary."""
        params = [MethodParam(name=p["name"], type_name=p["type_name"]) 
                  for p in data.get("params", [])]
        return cls(
            name=data["name"],
            return_type=data["return_type"],
            params=params,
            is_const=data.get("is_const", False),
            is_virtual=data.get("is_virtual", False),
            is_override=data.get("is_override", False),
            line=data.get("line", 0),
        )


@dataclass
class EnumValue:
    """Information about an enum constant."""
    name: str
    value: Optional[int] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {"name": self.name, "value": self.value}
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'EnumValue':
        """Create from dictionary."""
        return cls(name=data["name"], value=data.get("value"))


@dataclass
class EnumData:
    """Information about a reflected enum."""
    name: str
    qualified_name: str
    namespace: str
    underlying_type: str = "int"
    values: List[EnumValue] = field(default_factory=list)
    line: int = 0
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "qualified_name": self.qualified_name,
            "namespace": self.namespace,
            "underlying_type": self.underlying_type,
            "values": [v.to_dict() for v in self.values],
            "line": self.line,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'EnumData':
        """Create from dictionary."""
        values = [EnumValue.from_dict(v) for v in data.get("values", [])]
        return cls(
            name=data["name"],
            qualified_name=data["qualified_name"],
            namespace=data["namespace"],
            underlying_type=data.get("underlying_type", "int"),
            values=values,
            line=data.get("line", 0),
        )


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
    is_event: bool = False  # True if marked with BE_EVENT (lightweight, no _typeMeta)
    parent_class: Optional[str] = None  # Direct parent class name if inheriting
    source_file: str = ""
    line: int = 0
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "qualified_name": self.qualified_name,
            "full_qualified_name": self.full_qualified_name,
            "namespace": self.namespace,
            "fields": [f.to_dict() for f in self.fields],
            "methods": [m.to_dict() for m in self.methods],
            "is_factory_base": self.is_factory_base,
            "is_event": self.is_event,
            "parent_class": self.parent_class,
            "source_file": self.source_file,
            "line": self.line,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'ClassData':
        """Create from dictionary."""
        fields = [FieldData.from_dict(f) for f in data.get("fields", [])]
        methods = [MethodData.from_dict(m) for m in data.get("methods", [])]
        return cls(
            name=data["name"],
            qualified_name=data["qualified_name"],
            full_qualified_name=data["full_qualified_name"],
            namespace=data["namespace"],
            fields=fields,
            methods=methods,
            is_factory_base=data.get("is_factory_base", False),
            is_event=data.get("is_event", False),
            parent_class=data.get("parent_class"),
            source_file=data.get("source_file", ""),
            line=data.get("line", 0),
        )


@dataclass
class DerivedClassData:
    """Information about a derived class for factory generation."""
    name: str           # Short class name (e.g., "ConsoleSink")
    short_name: str     # Name for enum value (e.g., "Console" if strip suffix)
    full_name: str      # Full qualified name (e.g., "BECore::ConsoleSink")
    source_file: str    # Source file where this class is defined
    include_path: str = ""  # Include path for the header
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "short_name": self.short_name,
            "full_name": self.full_name,
            "source_file": self.source_file,
            "include_path": self.include_path,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'DerivedClassData':
        """Create from dictionary."""
        return cls(
            name=data["name"],
            short_name=data["short_name"],
            full_name=data["full_name"],
            source_file=data["source_file"],
            include_path=data.get("include_path", ""),
        )


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
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "qualified_name": self.qualified_name,
            "full_qualified_name": self.full_qualified_name,
            "namespace": self.namespace,
            "enum_type_name": self.enum_type_name,
            "factory_name": self.factory_name,
            "derived": [d.to_dict() for d in self.derived],
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FactoryBaseData':
        """Create from dictionary."""
        derived = [DerivedClassData.from_dict(d) for d in data.get("derived", [])]
        return cls(
            name=data["name"],
            qualified_name=data["qualified_name"],
            full_qualified_name=data["full_qualified_name"],
            namespace=data["namespace"],
            enum_type_name=data.get("enum_type_name", ""),
            factory_name=data.get("factory_name", ""),
            derived=derived,
        )


@dataclass
class FileMetadata:
    """Metadata for a single source file."""
    path: str
    content_hash: str  # SHA-256 hash of file contents
    last_scanned: str  # ISO timestamp
    classes: List[ClassData] = field(default_factory=list)
    enums: List[EnumData] = field(default_factory=list)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "path": self.path,
            "content_hash": self.content_hash,
            "last_scanned": self.last_scanned,
            "classes": [c.to_dict() for c in self.classes],
            "enums": [e.to_dict() for e in self.enums],
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'FileMetadata':
        """Create from dictionary."""
        classes = [ClassData.from_dict(c) for c in data.get("classes", [])]
        enums = [EnumData.from_dict(e) for e in data.get("enums", [])]
        return cls(
            path=data["path"],
            content_hash=data["content_hash"],
            last_scanned=data["last_scanned"],
            classes=classes,
            enums=enums,
        )
