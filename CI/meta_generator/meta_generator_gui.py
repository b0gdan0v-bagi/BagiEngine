#!/usr/bin/env python3
"""
BagiEngine Meta-Generator GUI

Standalone GUI application for:
- Viewing all reflected types from the metadata cache
- Inspecting class/enum/field/method details
- Running reflection code generation
- Configuring LLVM path

Usage:
    python meta_generator_gui.py [project_root]
"""

import sys
from pathlib import Path

# Ensure the package is importable
sys.path.insert(0, str(Path(__file__).parent))


def main():
    """Main entry point for GUI."""
    from PyQt6.QtWidgets import QApplication
    from PyQt6.QtCore import Qt
    
    from gui.main_window import MetaGeneratorWindow
    
    # Parse arguments
    project_root = None
    if len(sys.argv) > 1:
        project_root = Path(sys.argv[1])
        if not project_root.exists():
            print(f"Error: Project root does not exist: {project_root}")
            sys.exit(1)
    else:
        # Try to find project root by looking for CMakeLists.txt
        current = Path(__file__).parent
        while current != current.parent:
            if (current / "CMakeLists.txt").exists():
                project_root = current
                break
            current = current.parent
        
        if not project_root:
            project_root = Path.cwd()
    
    # Create application
    app = QApplication(sys.argv)
    app.setApplicationName("BagiEngine Meta-Generator")
    app.setOrganizationName("BagiEngine")
    
    # Set fusion style for consistent look
    app.setStyle("Fusion")
    
    # Create and show main window
    window = MetaGeneratorWindow(project_root)
    window.show()
    
    # Run event loop
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
