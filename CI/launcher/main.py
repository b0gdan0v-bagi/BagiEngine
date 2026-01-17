"""Entry point for the BagiEngine Launcher."""

import sys
from pathlib import Path

from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import Qt

from .main_window import MainWindow


def get_project_root() -> Path:
    """Get the project root directory."""
    # The launcher is in CI/launcher/, so project root is two levels up
    current_file = Path(__file__).resolve()
    return current_file.parent.parent.parent


def main():
    """Main entry point."""
    # Enable high DPI scaling
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )
    
    app = QApplication(sys.argv)
    app.setApplicationName("BagiEngine Launcher")
    app.setOrganizationName("BagiEngine")
    
    # Set application style
    app.setStyle("Fusion")
    
    project_root = get_project_root()
    
    window = MainWindow(project_root)
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
