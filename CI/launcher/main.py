"""Entry point for the BagiEngine Launcher."""

import sys
import os
from pathlib import Path

from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import Qt

from .main_window import MainWindow


def get_project_root() -> Path:
    """Get the project root directory."""
    # The launcher is in CI/launcher/, so project root is two levels up
    current_file = Path(__file__).resolve()
    return current_file.parent.parent.parent


def load_env_file(project_root: Path) -> None:
    """Load environment variables from .env file in project root."""
    try:
        from dotenv import load_dotenv
        env_file = project_root / ".env"
        if env_file.exists():
            # Load with override=True to overwrite existing system variables
            loaded = load_dotenv(env_file, override=True, encoding='utf-8')
            if loaded:
                print(f"[INFO] Loaded environment variables from: {env_file}")
                # Debug: print LIBCLANG_PATH if loaded
                libclang = os.getenv("LIBCLANG_PATH")
                if libclang:
                    print(f"[INFO] LIBCLANG_PATH = {libclang}")
            else:
                print(f"[WARNING] Failed to load .env file: {env_file}")
        else:
            print(f"[INFO] No .env file found at: {env_file}")
    except ImportError:
        print("[WARNING] python-dotenv not installed, .env file will not be loaded")
        print("[INFO] Install with: pip install python-dotenv")


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
    
    # Load environment variables from .env file
    load_env_file(project_root)
    
    window = MainWindow(project_root)
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
