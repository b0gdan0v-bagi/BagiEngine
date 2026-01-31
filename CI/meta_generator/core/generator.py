"""
Code Generator Module

Generates reflection code using Jinja2 templates.
Produces .gen.hpp files compatible with the existing reflection system.
"""

import os
from pathlib import Path
from typing import List, Optional

from jinja2 import Environment, FileSystemLoader

from .models import ClassData, EnumData, FactoryBaseData, DerivedClassData


# Default templates directory (relative to this file)
DEFAULT_TEMPLATES_DIR = Path(__file__).parent.parent / "templates"


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
    common_suffix = ""
    for i in range(1, min(len(class_name), len(base_suffix)) + 1):
        if class_name[-i:] == base_suffix[-i:]:
            common_suffix = class_name[-i:]
        else:
            break
    
    # Strip the common suffix from the class name
    if common_suffix and len(common_suffix) < len(class_name):
        short_name = class_name[:-len(common_suffix)]
    
    # If result is empty, use original
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
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]
    return f"{name}Type"


def compute_factory_name(base_name: str) -> str:
    """
    Compute factory class name from base class name.
    
    Strips 'I' prefix if present and adds 'Factory' suffix.
    
    Examples:
        ILogSink -> LogSinkFactory
        IWidget -> WidgetFactory
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]
    return f"{name}Factory"


def compute_include_path(source_file: str, include_dirs: List[str]) -> str:
    """
    Compute the relative include path for a source file.
    
    Tries to compute path relative to one of the include directories.
    Falls back to filename only if no include_dir matches.
    
    Examples:
        source: D:/Project/src/Widgets/ClearScreenWidget.h
        include_dirs: ["D:/Project/src"]
        -> Widgets/ClearScreenWidget.h
        
        source: D:/Project/src/Modules/BECore/Logger/ConsoleSink.h  
        include_dirs: ["D:/Project/src"]
        -> Modules/BECore/Logger/ConsoleSink.h
    """
    source_path = Path(source_file).resolve()
    
    # Try to make path relative to one of the include directories
    for inc_dir in include_dirs:
        inc_path = Path(inc_dir).resolve()
        try:
            rel_path = source_path.relative_to(inc_path)
            # Convert to forward slashes for cross-platform include paths
            return str(rel_path).replace('\\', '/')
        except ValueError:
            # source_path is not relative to this include directory
            continue
    
    # Fallback: use just the filename (less ideal but better than absolute path)
    return source_path.name


def compute_enum_output_filename(base_name: str) -> str:
    """
    Compute output filename for enum factory file.
    
    Examples:
        ILogSink -> EnumLogSink.gen.hpp
        IWidget -> EnumWidget.gen.hpp
    """
    name = base_name
    if name.startswith('I') and len(name) > 1:
        name = name[1:]
    return f"Enum{name}.gen.hpp"


class CodeGenerator:
    """
    Generates reflection code from parsed metadata.
    
    Uses Jinja2 templates to produce .gen.hpp files that are
    compatible with the existing BagiEngine reflection system.
    """
    
    def __init__(self, template_dir: Path = None, output_dir: Path = None):
        """
        Initialize code generator.
        
        Args:
            template_dir: Directory containing Jinja2 templates
            output_dir: Directory for generated files
        """
        self.template_dir = template_dir or DEFAULT_TEMPLATES_DIR
        self.output_dir = output_dir or Path("Generated")
        
        # Setup Jinja2 environment
        self.env = Environment(
            loader=FileSystemLoader(str(self.template_dir)),
            trim_blocks=True,
            lstrip_blocks=True
        )
    
    def generate_reflection(
        self, 
        classes: List[ClassData], 
        enums: List[EnumData], 
        source_file: Path,
        include_dirs: List[str] = None
    ) -> Optional[Path]:
        """
        Generate reflection code for classes and enums.
        
        Args:
            classes: List of reflected classes
            enums: List of reflected enums
            source_file: Path to the source file
            include_dirs: Include directories for computing include path
            
        Returns:
            Path to generated file, or None if nothing to generate
        """
        if not classes and not enums:
            return None
        
        # Ensure output directory exists
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Load template
        template = self.env.get_template('reflection.gen.hpp.j2')
        
        # Compute include path for the original header
        include_path = compute_include_path(str(source_file), include_dirs or [])
        
        # Prepare class data for template (convert dataclasses to dicts)
        classes_data = []
        for cls in classes:
            cls_dict = {
                'name': cls.name,
                'qualified_name': cls.qualified_name,
                'full_qualified_name': cls.full_qualified_name,
                'namespace': cls.namespace,
                'fields': [
                    {
                        'name': f.name,
                        'type_name': f.type_name,
                        'is_primitive': f.is_primitive,
                    }
                    for f in cls.fields
                ],
                'methods': [
                    {
                        'name': m.name,
                        'return_type': m.return_type,
                        'params': [{'name': p.name, 'type_name': p.type_name} for p in m.params],
                        'is_const': m.is_const,
                        'is_virtual': m.is_virtual,
                        'is_override': m.is_override,
                    }
                    for m in cls.methods
                ],
                'is_factory_base': cls.is_factory_base,
                'is_event': cls.is_event,
                'parent_class': cls.parent_class,
            }
            classes_data.append(cls_dict)
        
        # Prepare enum data
        enums_data = []
        for enum in enums:
            enum_dict = {
                'name': enum.name,
                'qualified_name': enum.qualified_name,
                'namespace': enum.namespace,
                'underlying_type': enum.underlying_type,
                'values': [{'name': v.name} for v in enum.values],
            }
            enums_data.append(enum_dict)
        
        # Render template
        output = template.render(
            input_filename=source_file.name,
            include_path=include_path,
            classes=classes_data,
            enums=enums_data,
            factory_bases=[]  # Factory bases are generated separately
        )
        
        # Write output file
        output_filename = source_file.stem + ".gen.hpp"
        output_path = self.output_dir / output_filename
        
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(output)
        
        return output_path
    
    def generate_enum_factory(
        self,
        factory_base: FactoryBaseData,
        input_filename: str
    ) -> Optional[Path]:
        """
        Generate enum and factory code for a factory base class.
        
        Args:
            factory_base: Factory base data with derived classes
            input_filename: Name of the base class source file
            
        Returns:
            Path to generated file, or None if no derived classes
        """
        if not factory_base.derived:
            return None
        
        # Ensure output directory exists
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Load template
        template = self.env.get_template('enum_factory.gen.hpp.j2')
        
        # Prepare factory base data
        base_dict = {
            'name': factory_base.name,
            'qualified_name': factory_base.qualified_name,
            'full_qualified_name': factory_base.full_qualified_name,
            'namespace': factory_base.namespace,
            'enum_type_name': factory_base.enum_type_name,
            'factory_name': factory_base.factory_name,
            'derived': [
                {
                    'name': d.name,
                    'short_name': d.short_name,
                    'full_name': d.full_name,
                    'source_file': d.source_file,
                    'include_path': d.include_path,
                }
                for d in factory_base.derived
            ],
        }
        
        # Render template
        output = template.render(
            input_filename=input_filename,
            factory_bases=[base_dict]
        )
        
        # Write output file
        output_filename = compute_enum_output_filename(factory_base.name)
        output_path = self.output_dir / output_filename
        
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(output)
        
        return output_path
    
    def build_factory_bases(
        self,
        all_classes: List[ClassData],
        include_dirs: List[str] = None
    ) -> List[FactoryBaseData]:
        """
        Build factory base data by matching derived classes to base classes.
        
        Args:
            all_classes: All parsed classes from cache
            include_dirs: Include directories for computing include paths
            
        Returns:
            List of FactoryBaseData with derived classes populated
        """
        if include_dirs is None:
            include_dirs = []
        
        factory_bases: List[FactoryBaseData] = []
        
        # Find all factory base classes
        base_classes = [cls for cls in all_classes if cls.is_factory_base]
        
        for base_cls in base_classes:
            factory_base = FactoryBaseData(
                name=base_cls.name,
                qualified_name=base_cls.qualified_name,
                full_qualified_name=base_cls.full_qualified_name,
                namespace=base_cls.namespace,
                enum_type_name=compute_enum_type_name(base_cls.name),
                factory_name=compute_factory_name(base_cls.name)
            )
            
            # Find all derived classes
            for cls in all_classes:
                if cls.parent_class == base_cls.name:
                    short_name = compute_short_name(cls.name, base_cls.name)
                    include_path = compute_include_path(cls.source_file, include_dirs)
                    
                    derived = DerivedClassData(
                        name=cls.name,
                        short_name=short_name,
                        full_name=cls.full_qualified_name,
                        source_file=cls.source_file,
                        include_path=include_path
                    )
                    factory_base.derived.append(derived)
            
            if factory_base.derived:
                factory_bases.append(factory_base)
        
        return factory_bases
    
    def should_regenerate(self, source_path: Path, output_path: Path) -> bool:
        """
        Check if generated file needs to be regenerated.
        
        Args:
            source_path: Path to source file
            output_path: Path to generated file
            
        Returns:
            True if regeneration is needed
        """
        if not output_path.exists():
            return True
        
        try:
            source_mtime = source_path.stat().st_mtime
            output_mtime = output_path.stat().st_mtime
            return source_mtime > output_mtime
        except OSError:
            return True
