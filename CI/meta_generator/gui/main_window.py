"""
Main Window for Meta-Generator GUI

Provides a visual interface for:
- Viewing all reflected types from the metadata cache
- Inspecting class/enum details
- Running full rescan or incremental generation
- Configuring LLVM path
"""

import json
from pathlib import Path
from typing import Optional

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QToolBar, QStatusBar, QLabel,
    QTextEdit, QMessageBox, QFileDialog
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QFont

from .type_tree import TypeTreeWidget
from .inspector import MetadataInspector
from .settings_dialog import SettingsDialog

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from core.cache import MetadataCache
from core.env_setup import initialize_clang, LLVMStatus


class GeneratorWorker(QThread):
    """Worker thread for running meta_generator CLI."""
    
    output_received = pyqtSignal(str)
    finished_signal = pyqtSignal(bool, str)  # success, message
    
    def __init__(self, project_root: Path, source_dirs: list, output_dir: Path, 
                 cache_dir: Path, include_dirs: list = None, force: bool = False):
        super().__init__()
        self.project_root = project_root
        self.source_dirs = source_dirs
        self.output_dir = output_dir
        self.cache_dir = cache_dir
        self.include_dirs = include_dirs or []
        self.force = force
    
    def run(self):
        import subprocess
        
        # Build command
        cmd = [
            sys.executable,
            str(self.project_root / "CI" / "meta_generator" / "meta_generator.py"),
            "--output-dir", str(self.output_dir),
            "--cache-dir", str(self.cache_dir),
            "--verbose"
        ]
        
        for src_dir in self.source_dirs:
            cmd.extend(["--source-dir", str(src_dir)])
        
        for inc_dir in self.include_dirs:
            cmd.extend(["--include-dir", str(inc_dir)])
        
        if self.force:
            cmd.append("--force")
        
        try:
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=str(self.project_root)
            )
            
            for line in process.stdout:
                self.output_received.emit(line.rstrip())
            
            process.wait()
            
            if process.returncode == 0:
                self.finished_signal.emit(True, "Generation completed successfully")
            else:
                self.finished_signal.emit(False, f"Generation failed with exit code {process.returncode}")
                
        except Exception as e:
            self.finished_signal.emit(False, f"Error: {str(e)}")


class MetaGeneratorWindow(QMainWindow):
    """Main window for Meta-Generator GUI."""
    
    def __init__(self, project_root: Path = None):
        super().__init__()
        
        self.project_root = project_root or Path.cwd()
        self.settings_path = self.project_root / "meta_generator_settings.json"
        self.cache: Optional[MetadataCache] = None
        self.worker: Optional[GeneratorWorker] = None
        
        self.setWindowTitle("BagiEngine Meta-Generator")
        self.setMinimumSize(1200, 800)
        
        self._setup_ui()
        self._setup_menu()
        self._setup_toolbar()
        self._setup_statusbar()
        self._load_settings()
        self._check_llvm_status()
        self._load_cache()
    
    def _setup_ui(self):
        """Setup the main UI layout."""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(0, 0, 0, 0)
        
        # Create main splitter (horizontal)
        main_splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # Left panel: Type tree
        self.type_tree = TypeTreeWidget()
        self.type_tree.item_selected.connect(self._on_item_selected)
        main_splitter.addWidget(self.type_tree)
        
        # Right side: vertical splitter with inspector and log
        right_splitter = QSplitter(Qt.Orientation.Vertical)
        
        # Inspector panel
        self.inspector = MetadataInspector()
        right_splitter.addWidget(self.inspector)
        
        # Log panel
        log_widget = QWidget()
        log_layout = QVBoxLayout(log_widget)
        log_layout.setContentsMargins(5, 5, 5, 5)
        
        log_label = QLabel("Generation Log")
        log_label.setStyleSheet("font-weight: bold;")
        log_layout.addWidget(log_label)
        
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Consolas", 9))
        self.log_text.setMaximumHeight(200)
        log_layout.addWidget(self.log_text)
        
        right_splitter.addWidget(log_widget)
        right_splitter.setSizes([500, 200])
        
        main_splitter.addWidget(right_splitter)
        main_splitter.setSizes([350, 850])
        
        main_layout.addWidget(main_splitter)
    
    def _setup_menu(self):
        """Setup menu bar."""
        menubar = self.menuBar()
        
        # File menu
        file_menu = menubar.addMenu("File")
        
        open_project_action = QAction("Open Project...", self)
        open_project_action.triggered.connect(self._open_project)
        file_menu.addAction(open_project_action)
        
        file_menu.addSeparator()
        
        refresh_action = QAction("Refresh Cache", self)
        refresh_action.setShortcut("F5")
        refresh_action.triggered.connect(self._load_cache)
        file_menu.addAction(refresh_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("Exit", self)
        exit_action.setShortcut("Alt+F4")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # Tools menu
        tools_menu = menubar.addMenu("Tools")
        
        settings_action = QAction("Settings...", self)
        settings_action.triggered.connect(self._open_settings)
        tools_menu.addAction(settings_action)
    
    def _setup_toolbar(self):
        """Setup toolbar."""
        toolbar = QToolBar("Main Toolbar")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        
        # Full Rescan button
        self.rescan_action = QAction("Full Rescan", self)
        self.rescan_action.setToolTip("Force re-parse all files (ignore cache)")
        self.rescan_action.triggered.connect(self._run_full_rescan)
        toolbar.addAction(self.rescan_action)
        
        # Generate button
        self.generate_action = QAction("Generate", self)
        self.generate_action.setToolTip("Generate reflection code (incremental)")
        self.generate_action.triggered.connect(self._run_generate)
        toolbar.addAction(self.generate_action)
        
        toolbar.addSeparator()
        
        # Refresh button
        refresh_action = QAction("Refresh", self)
        refresh_action.setToolTip("Reload cache from disk (F5)")
        refresh_action.triggered.connect(self._load_cache)
        toolbar.addAction(refresh_action)
        
        toolbar.addSeparator()
        
        # Settings button
        settings_action = QAction("Settings", self)
        settings_action.triggered.connect(self._open_settings)
        toolbar.addAction(settings_action)
    
    def _setup_statusbar(self):
        """Setup status bar."""
        self.statusbar = QStatusBar()
        self.setStatusBar(self.statusbar)
        
        # LLVM status indicator
        self.llvm_status_label = QLabel()
        self.statusbar.addPermanentWidget(self.llvm_status_label)
        
        # Statistics label
        self.stats_label = QLabel()
        self.statusbar.addWidget(self.stats_label)
    
    def _check_llvm_status(self):
        """Check and display LLVM status."""
        status = initialize_clang(self.settings_path, self.project_root)
        
        if status.found:
            self.llvm_status_label.setText(f"LLVM: OK ({status.source})")
            self.llvm_status_label.setStyleSheet("color: green; font-weight: bold;")
        else:
            self.llvm_status_label.setText("LLVM: Not configured (using regex)")
            self.llvm_status_label.setStyleSheet("color: orange;")
    
    def _load_settings(self):
        """Load window settings from JSON."""
        if not self.settings_path.exists():
            return
        
        try:
            with open(self.settings_path, 'r', encoding='utf-8') as f:
                settings = json.load(f)
            
            geometry = settings.get("window_geometry", {})
            if geometry:
                self.resize(geometry.get("width", 1200), geometry.get("height", 800))
                if "x" in geometry and "y" in geometry:
                    self.move(geometry["x"], geometry["y"])
                    
        except Exception:
            pass
    
    def _save_settings(self):
        """Save window settings to JSON."""
        settings = {}
        
        # Load existing settings
        if self.settings_path.exists():
            try:
                with open(self.settings_path, 'r', encoding='utf-8') as f:
                    settings = json.load(f)
            except Exception:
                pass
        
        # Update geometry
        settings["window_geometry"] = {
            "width": self.width(),
            "height": self.height(),
            "x": self.x(),
            "y": self.y()
        }
        
        # Save
        try:
            with open(self.settings_path, 'w', encoding='utf-8') as f:
                json.dump(settings, f, indent=2)
        except Exception:
            pass
    
    def _load_cache(self):
        """Load metadata cache and populate the tree."""
        # Try to find cache in common build directories
        possible_cache_paths = [
            self.project_root / "build" / "metadata_cache.json",
            self.project_root / "build" / "Debug" / "metadata_cache.json",
            self.project_root / "build" / "Release" / "metadata_cache.json",
        ]
        
        # Also check for compiler-specific build dirs
        for compiler in ["MSVC", "Clang", "GCC"]:
            for platform in ["Windows", "Linux", "macOS"]:
                possible_cache_paths.append(
                    self.project_root / "build" / f"{compiler}{platform}" / "metadata_cache.json"
                )
                possible_cache_paths.append(
                    self.project_root / "build" / f"{compiler}{platform}Solution" / "metadata_cache.json"
                )
        
        cache_path = None
        for path in possible_cache_paths:
            if path.exists():
                cache_path = path
                break
        
        if cache_path:
            self.cache = MetadataCache(cache_path)
            if self.cache.load():
                self._update_tree()
                self._update_stats()
                self._log(f"Loaded cache from {cache_path}")
            else:
                self._log(f"Failed to load cache from {cache_path}")
        else:
            self._log("No cache found. Run 'Generate' to create metadata cache.")
            self.stats_label.setText("No cache loaded")
    
    def _update_tree(self):
        """Update the type tree with cache data."""
        if self.cache:
            classes = self.cache.get_all_classes()
            enums = self.cache.get_all_enums()
            self.type_tree.populate(classes, enums)
    
    def _update_stats(self):
        """Update statistics in status bar."""
        if self.cache:
            stats = self.cache.get_statistics()
            self.stats_label.setText(
                f"Files: {stats['files']} | Classes: {stats['classes']} | "
                f"Enums: {stats['enums']} | Fields: {stats['fields']} | Methods: {stats['methods']}"
            )
    
    def _on_item_selected(self, item_type: str, item_data):
        """Handle item selection in tree."""
        self.inspector.show_item(item_type, item_data)
    
    def _open_project(self):
        """Open a different project directory."""
        path = QFileDialog.getExistingDirectory(
            self, "Select Project Root",
            str(self.project_root)
        )
        if path:
            self.project_root = Path(path)
            self.settings_path = self.project_root / "meta_generator_settings.json"
            self._load_settings()
            self._check_llvm_status()
            self._load_cache()
            self.setWindowTitle(f"BagiEngine Meta-Generator - {self.project_root.name}")
    
    def _open_settings(self):
        """Open settings dialog."""
        dialog = SettingsDialog(self, self.project_root)
        if dialog.exec():
            self._check_llvm_status()
    
    def _run_generate(self):
        """Run incremental generation."""
        self._run_generator(force=False)
    
    def _run_full_rescan(self):
        """Run full rescan (ignore cache)."""
        reply = QMessageBox.question(
            self, "Confirm Full Rescan",
            "This will re-parse all files, ignoring the cache.\nContinue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._run_generator(force=True)
    
    def _run_generator(self, force: bool = False):
        """Run the meta generator."""
        if self.worker and self.worker.isRunning():
            QMessageBox.warning(self, "Busy", "Generator is already running.")
            return
        
        # Determine directories
        source_dirs = [
            self.project_root / "src" / "Modules" / "BECore" / "Logger",
            self.project_root / "src" / "Modules" / "BECore" / "Tests",
            self.project_root / "src" / "Modules" / "BECore" / "Widgets",
            self.project_root / "src" / "Widgets",
        ]
        source_dirs = [d for d in source_dirs if d.exists()]
        
        if not source_dirs:
            QMessageBox.warning(self, "No Sources", "No source directories found.")
            return
        
        # Find or create output directory
        output_dir = self.project_root / "build" / "Generated"
        cache_dir = self.project_root / "build"
        
        include_dirs = [
            str(self.project_root / "src" / "Modules"),
            str(self.project_root / "src"),
        ]
        
        self.log_text.clear()
        self._log("Starting generation..." + (" (force rescan)" if force else ""))
        
        self.rescan_action.setEnabled(False)
        self.generate_action.setEnabled(False)
        
        self.worker = GeneratorWorker(
            self.project_root, source_dirs, output_dir, cache_dir, include_dirs, force
        )
        self.worker.output_received.connect(self._log)
        self.worker.finished_signal.connect(self._on_generation_finished)
        self.worker.start()
    
    def _on_generation_finished(self, success: bool, message: str):
        """Handle generation completion."""
        self.rescan_action.setEnabled(True)
        self.generate_action.setEnabled(True)
        
        if success:
            self._log(f"\n{message}")
            self._load_cache()
        else:
            self._log(f"\nERROR: {message}")
            QMessageBox.warning(self, "Generation Failed", message)
    
    def _log(self, message: str):
        """Append message to log."""
        self.log_text.append(message)
        # Scroll to bottom
        scrollbar = self.log_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def closeEvent(self, event):
        """Handle window close."""
        self._save_settings()
        
        if self.worker and self.worker.isRunning():
            self.worker.terminate()
            self.worker.wait()
        
        event.accept()
