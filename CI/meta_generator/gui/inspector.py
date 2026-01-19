"""
Metadata Inspector Panel for Meta-Generator

Displays detailed information about selected types, fields, and methods.
"""

from pathlib import Path
from typing import Any, Optional

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QLabel, 
    QGroupBox, QTextEdit, QScrollArea
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from core.models import ClassData, EnumData, FieldData, MethodData, EnumValue


class MetadataInspector(QScrollArea):
    """Inspector panel for viewing item details."""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        
        # Container widget
        self.container = QWidget()
        self.setWidget(self.container)
        
        self.layout = QVBoxLayout(self.container)
        self.layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        
        # Title
        self.title_label = QLabel("Select an item to view details")
        self.title_label.setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;")
        self.layout.addWidget(self.title_label)
        
        # Content area (will be populated dynamically)
        self.content_widget = QWidget()
        self.content_layout = QVBoxLayout(self.content_widget)
        self.content_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.layout.addWidget(self.content_widget)
        
        self.layout.addStretch()
    
    def show_item(self, item_type: str, item_data: Any):
        """
        Show details for an item.
        
        Args:
            item_type: Type of item ("class", "enum", "field", "method", "enum_value")
            item_data: The data object
        """
        # Clear previous content
        self._clear_content()
        
        if item_type == "class":
            self._show_class(item_data)
        elif item_type == "enum":
            self._show_enum(item_data)
        elif item_type == "field":
            self._show_field(item_data)
        elif item_type == "method":
            self._show_method(item_data)
        elif item_type == "enum_value":
            self._show_enum_value(item_data)
        else:
            self.title_label.setText("Unknown item type")
    
    def _clear_content(self):
        """Clear the content area."""
        while self.content_layout.count():
            child = self.content_layout.takeAt(0)
            if child.widget():
                child.widget().deleteLater()
    
    def _show_class(self, cls: ClassData):
        """Show class details."""
        self.title_label.setText(f"Class: {cls.name}")
        
        # Basic info group
        info_group = QGroupBox("Basic Information")
        info_layout = QFormLayout(info_group)
        
        info_layout.addRow("Name:", QLabel(cls.name))
        info_layout.addRow("Qualified Name:", QLabel(cls.qualified_name))
        info_layout.addRow("Full Qualified Name:", QLabel(cls.full_qualified_name))
        info_layout.addRow("Namespace:", QLabel(cls.namespace or "(global)"))
        
        if cls.parent_class:
            info_layout.addRow("Parent Class:", QLabel(cls.parent_class))
        
        factory_label = QLabel("Yes" if cls.is_factory_base else "No")
        if cls.is_factory_base:
            factory_label.setStyleSheet("color: green; font-weight: bold;")
        info_layout.addRow("Factory Base:", factory_label)
        
        info_layout.addRow("Source File:", QLabel(Path(cls.source_file).name if cls.source_file else "-"))
        info_layout.addRow("Line:", QLabel(str(cls.line) if cls.line else "-"))
        
        self.content_layout.addWidget(info_group)
        
        # Fields summary
        if cls.fields:
            fields_group = QGroupBox(f"Fields ({len(cls.fields)})")
            fields_layout = QVBoxLayout(fields_group)
            
            fields_text = QTextEdit()
            fields_text.setReadOnly(True)
            fields_text.setMaximumHeight(150)
            fields_text.setFont(QFont("Consolas", 9))
            
            text = ""
            for field in cls.fields:
                text += f"{field.type_name} {field.name};\n"
            fields_text.setPlainText(text)
            
            fields_layout.addWidget(fields_text)
            self.content_layout.addWidget(fields_group)
        
        # Methods summary
        if cls.methods:
            methods_group = QGroupBox(f"Methods ({len(cls.methods)})")
            methods_layout = QVBoxLayout(methods_group)
            
            methods_text = QTextEdit()
            methods_text.setReadOnly(True)
            methods_text.setMaximumHeight(150)
            methods_text.setFont(QFont("Consolas", 9))
            
            text = ""
            for method in cls.methods:
                params = ", ".join(f"{p.type_name} {p.name}" for p in method.params)
                const = " const" if method.is_const else ""
                override = " override" if method.is_override else ""
                virtual = "virtual " if method.is_virtual else ""
                text += f"{virtual}{method.return_type} {method.name}({params}){const}{override};\n"
            methods_text.setPlainText(text)
            
            methods_layout.addWidget(methods_text)
            self.content_layout.addWidget(methods_group)
    
    def _show_enum(self, enum: EnumData):
        """Show enum details."""
        self.title_label.setText(f"Enum: {enum.name}")
        
        # Basic info
        info_group = QGroupBox("Basic Information")
        info_layout = QFormLayout(info_group)
        
        info_layout.addRow("Name:", QLabel(enum.name))
        info_layout.addRow("Qualified Name:", QLabel(enum.qualified_name))
        info_layout.addRow("Namespace:", QLabel(enum.namespace or "(global)"))
        info_layout.addRow("Underlying Type:", QLabel(enum.underlying_type))
        info_layout.addRow("Value Count:", QLabel(str(len(enum.values))))
        
        self.content_layout.addWidget(info_group)
        
        # Values
        if enum.values:
            values_group = QGroupBox(f"Values ({len(enum.values)})")
            values_layout = QVBoxLayout(values_group)
            
            values_text = QTextEdit()
            values_text.setReadOnly(True)
            values_text.setMaximumHeight(200)
            values_text.setFont(QFont("Consolas", 9))
            
            text = ""
            for i, value in enumerate(enum.values):
                text += f"{value.name}"
                if value.value is not None:
                    text += f" = {value.value}"
                text += ",\n" if i < len(enum.values) - 1 else "\n"
            values_text.setPlainText(text)
            
            values_layout.addWidget(values_text)
            self.content_layout.addWidget(values_group)
    
    def _show_field(self, field: FieldData):
        """Show field details."""
        self.title_label.setText(f"Field: {field.name}")
        
        info_group = QGroupBox("Field Information")
        info_layout = QFormLayout(info_group)
        
        info_layout.addRow("Name:", QLabel(field.name))
        info_layout.addRow("Type:", QLabel(field.type_name))
        info_layout.addRow("Line:", QLabel(str(field.line) if field.line else "-"))
        info_layout.addRow("Column:", QLabel(str(field.column) if field.column else "-"))
        
        self.content_layout.addWidget(info_group)
    
    def _show_method(self, method: MethodData):
        """Show method details."""
        self.title_label.setText(f"Method: {method.name}")
        
        info_group = QGroupBox("Method Information")
        info_layout = QFormLayout(info_group)
        
        info_layout.addRow("Name:", QLabel(method.name))
        info_layout.addRow("Return Type:", QLabel(method.return_type))
        info_layout.addRow("Is Const:", QLabel("Yes" if method.is_const else "No"))
        info_layout.addRow("Is Virtual:", QLabel("Yes" if method.is_virtual else "No"))
        info_layout.addRow("Is Override:", QLabel("Yes" if method.is_override else "No"))
        info_layout.addRow("Line:", QLabel(str(method.line) if method.line else "-"))
        
        self.content_layout.addWidget(info_group)
        
        # Parameters
        if method.params:
            params_group = QGroupBox(f"Parameters ({len(method.params)})")
            params_layout = QFormLayout(params_group)
            
            for i, param in enumerate(method.params):
                params_layout.addRow(f"[{i}] {param.name}:", QLabel(param.type_name))
            
            self.content_layout.addWidget(params_group)
        
        # Signature preview
        sig_group = QGroupBox("Signature")
        sig_layout = QVBoxLayout(sig_group)
        
        params_str = ", ".join(f"{p.type_name} {p.name}" for p in method.params)
        const = " const" if method.is_const else ""
        override = " override" if method.is_override else ""
        virtual = "virtual " if method.is_virtual else ""
        signature = f"{virtual}{method.return_type} {method.name}({params_str}){const}{override};"
        
        sig_label = QLabel(signature)
        sig_label.setFont(QFont("Consolas", 10))
        sig_label.setWordWrap(True)
        sig_layout.addWidget(sig_label)
        
        self.content_layout.addWidget(sig_group)
    
    def _show_enum_value(self, value: EnumValue):
        """Show enum value details."""
        self.title_label.setText(f"Enum Value: {value.name}")
        
        info_group = QGroupBox("Value Information")
        info_layout = QFormLayout(info_group)
        
        info_layout.addRow("Name:", QLabel(value.name))
        if value.value is not None:
            info_layout.addRow("Value:", QLabel(str(value.value)))
        
        self.content_layout.addWidget(info_group)
