"""
Type Tree Widget for Meta-Generator

Displays all reflected classes and enums in a hierarchical tree view.
"""

from pathlib import Path
from typing import List, Optional, Any

from PyQt6.QtWidgets import QTreeWidget, QTreeWidgetItem
from PyQt6.QtCore import pyqtSignal, Qt
from PyQt6.QtGui import QFont

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from core.models import ClassData, EnumData


class TypeTreeWidget(QTreeWidget):
    """Tree widget for displaying reflected types."""
    
    # Signal emitted when an item is selected
    # Args: item_type ("class", "enum", "field", "method"), item_data
    item_selected = pyqtSignal(str, object)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.setHeaderLabels(["Name", "Details"])
        self.setColumnWidth(0, 250)
        self.setAlternatingRowColors(True)
        
        # Store data references
        self._classes: List[ClassData] = []
        self._enums: List[EnumData] = []
        
        # Connect selection signal
        self.itemSelectionChanged.connect(self._on_selection_changed)
    
    def populate(self, classes: List[ClassData], enums: List[EnumData]):
        """
        Populate the tree with classes and enums.
        
        Args:
            classes: List of ClassData objects
            enums: List of EnumData objects
        """
        self.clear()
        self._classes = classes
        self._enums = enums
        
        # Create root nodes
        classes_root = QTreeWidgetItem(self, ["Classes", f"({len(classes)})"])
        classes_root.setExpanded(True)
        font = classes_root.font(0)
        font.setBold(True)
        classes_root.setFont(0, font)
        
        enums_root = QTreeWidgetItem(self, ["Enums", f"({len(enums)})"])
        enums_root.setExpanded(True)
        enums_root.setFont(0, font)
        
        # Group classes by namespace
        namespaces: dict[str, List[ClassData]] = {}
        for cls in classes:
            ns = cls.namespace or "(global)"
            if ns not in namespaces:
                namespaces[ns] = []
            namespaces[ns].append(cls)
        
        # Add classes by namespace
        for ns, ns_classes in sorted(namespaces.items()):
            ns_item = QTreeWidgetItem(classes_root, [ns, f"({len(ns_classes)})"])
            ns_item.setExpanded(True)
            
            for cls in sorted(ns_classes, key=lambda c: c.name):
                self._add_class_item(ns_item, cls)
        
        # Group enums by namespace
        enum_namespaces: dict[str, List[EnumData]] = {}
        for enum in enums:
            ns = enum.namespace or "(global)"
            if ns not in enum_namespaces:
                enum_namespaces[ns] = []
            enum_namespaces[ns].append(enum)
        
        # Add enums by namespace
        for ns, ns_enums in sorted(enum_namespaces.items()):
            ns_item = QTreeWidgetItem(enums_root, [ns, f"({len(ns_enums)})"])
            ns_item.setExpanded(True)
            
            for enum in sorted(ns_enums, key=lambda e: e.name):
                self._add_enum_item(ns_item, enum)
    
    def _add_class_item(self, parent: QTreeWidgetItem, cls: ClassData):
        """Add a class item with its fields and methods."""
        # Class node
        details = []
        if cls.is_factory_base:
            details.append("FACTORY_BASE")
        if cls.parent_class:
            details.append(f": {cls.parent_class}")
        
        class_item = QTreeWidgetItem(parent, [cls.name, " ".join(details)])
        class_item.setData(0, Qt.ItemDataRole.UserRole, ("class", cls))
        
        # Make factory bases bold
        if cls.is_factory_base:
            font = class_item.font(0)
            font.setBold(True)
            class_item.setFont(0, font)
        
        # Fields section
        if cls.fields:
            fields_item = QTreeWidgetItem(class_item, ["Fields", f"({len(cls.fields)})"])
            fields_item.setForeground(0, Qt.GlobalColor.darkGray)
            
            for field in cls.fields:
                field_item = QTreeWidgetItem(fields_item, [field.name, field.type_name])
                field_item.setData(0, Qt.ItemDataRole.UserRole, ("field", field))
                field_item.setForeground(1, Qt.GlobalColor.darkBlue)
        
        # Methods section
        if cls.methods:
            methods_item = QTreeWidgetItem(class_item, ["Methods", f"({len(cls.methods)})"])
            methods_item.setForeground(0, Qt.GlobalColor.darkGray)
            
            for method in cls.methods:
                params = ", ".join(p.type_name for p in method.params)
                signature = f"{method.return_type} ({params})"
                if method.is_const:
                    signature += " const"
                
                method_item = QTreeWidgetItem(methods_item, [method.name, signature])
                method_item.setData(0, Qt.ItemDataRole.UserRole, ("method", method))
                method_item.setForeground(1, Qt.GlobalColor.darkGreen)
    
    def _add_enum_item(self, parent: QTreeWidgetItem, enum: EnumData):
        """Add an enum item with its values."""
        # Enum node
        enum_item = QTreeWidgetItem(parent, [enum.name, f": {enum.underlying_type}"])
        enum_item.setData(0, Qt.ItemDataRole.UserRole, ("enum", enum))
        
        # Values
        for value in enum.values:
            value_item = QTreeWidgetItem(enum_item, [value.name, ""])
            value_item.setData(0, Qt.ItemDataRole.UserRole, ("enum_value", value))
            value_item.setForeground(0, Qt.GlobalColor.darkMagenta)
    
    def _on_selection_changed(self):
        """Handle selection change."""
        items = self.selectedItems()
        if not items:
            return
        
        item = items[0]
        data = item.data(0, Qt.ItemDataRole.UserRole)
        
        if data:
            item_type, item_data = data
            self.item_selected.emit(item_type, item_data)
    
    def find_class(self, class_name: str) -> Optional[ClassData]:
        """Find a class by name."""
        for cls in self._classes:
            if cls.name == class_name:
                return cls
        return None
    
    def find_enum(self, enum_name: str) -> Optional[EnumData]:
        """Find an enum by name."""
        for enum in self._enums:
            if enum.name == enum_name:
                return enum
        return None
