"""
Settings Dialog for Meta-Generator

Allows configuration of:
- LLVM/libclang path
- Other generator options
"""

import json
from pathlib import Path
from typing import Optional

from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QFormLayout,
    QLineEdit, QPushButton, QLabel, QFileDialog,
    QDialogButtonBox, QGroupBox, QMessageBox
)
from PyQt6.QtCore import Qt

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from core.env_setup import LLVMDiscovery


class SettingsDialog(QDialog):
    """Settings dialog for Meta-Generator."""
    
    def __init__(self, parent=None, project_root: Path = None):
        super().__init__(parent)
        
        # Settings are stored in repository root (meta_generator_settings.json)
        # This file is in .gitignore - each developer has their own settings
        self.project_root = project_root or Path.cwd()
        self.settings_path = self.project_root / "meta_generator_settings.json"
        
        self.setWindowTitle("Meta-Generator Settings")
        self.setMinimumWidth(550)
        
        self._setup_ui()
        self._load_settings()
        self._validate_llvm()
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        # LLVM Settings Group
        llvm_group = QGroupBox("LLVM / Libclang")
        llvm_layout = QFormLayout(llvm_group)
        
        # LLVM Path input
        path_layout = QHBoxLayout()
        self.llvm_path_edit = QLineEdit()
        self.llvm_path_edit.setPlaceholderText("Auto-detect or specify path to LLVM bin directory")
        self.llvm_path_edit.textChanged.connect(self._on_path_changed)
        path_layout.addWidget(self.llvm_path_edit)
        
        browse_btn = QPushButton("...")
        browse_btn.setFixedWidth(30)
        browse_btn.setToolTip("Browse for LLVM directory")
        browse_btn.clicked.connect(self._browse_llvm_path)
        path_layout.addWidget(browse_btn)
        
        auto_detect_btn = QPushButton("Auto-detect")
        auto_detect_btn.setToolTip("Try to automatically find LLVM installation")
        auto_detect_btn.clicked.connect(self._auto_detect_llvm)
        path_layout.addWidget(auto_detect_btn)
        
        llvm_layout.addRow("LLVM Bin Path:", path_layout)
        
        # Status indicator
        self.status_label = QLabel()
        llvm_layout.addRow("Status:", self.status_label)
        
        # Version info
        self.version_label = QLabel()
        llvm_layout.addRow("Verification:", self.version_label)
        
        layout.addWidget(llvm_group)
        
        # Help text
        help_text = QLabel(
            "LLVM/libclang is REQUIRED for reflection generation.\n\n"
            "If auto-detection fails, manually specify the path to LLVM's bin directory.\n"
            "The directory should contain libclang.dll (Windows) or libclang.so (Linux).\n\n"
            "Download LLVM from: https://github.com/llvm/llvm-project/releases"
        )
        help_text.setStyleSheet("color: gray; font-size: 11px;")
        help_text.setWordWrap(True)
        layout.addWidget(help_text)
        
        layout.addStretch()
        
        # Buttons
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save_and_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
    
    def _browse_llvm_path(self):
        """Browse for LLVM directory."""
        start_path = self.llvm_path_edit.text() or ""
        
        path = QFileDialog.getExistingDirectory(
            self, "Select LLVM Bin Directory", start_path
        )
        if path:
            self.llvm_path_edit.setText(path)
    
    def _auto_detect_llvm(self):
        """Auto-detect LLVM installation."""
        discovery = LLVMDiscovery()
        status = discovery.discover()
        
        if status.found:
            self.llvm_path_edit.setText(status.path)
            QMessageBox.information(
                self, "LLVM Found",
                f"LLVM detected via {status.source}:\n{status.path}"
            )
        else:
            QMessageBox.warning(
                self, "LLVM Not Found",
                "Could not auto-detect LLVM installation.\n"
                "Please install LLVM or specify path manually.\n\n"
                "Download from:\nhttps://github.com/llvm/llvm-project/releases"
            )
    
    def _on_path_changed(self):
        """Handle path text change."""
        self._validate_llvm()
    
    def _validate_llvm(self):
        """Validate the LLVM path."""
        path = self.llvm_path_edit.text().strip()
        
        if not path:
            self.status_label.setText("Not configured (REQUIRED)")
            self.status_label.setStyleSheet("color: red; font-weight: bold;")
            self.version_label.setText("-")
            return
        
        discovery = LLVMDiscovery()
        
        if discovery._validate_path(path):
            self.status_label.setText("OK - libclang found")
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            
            version = discovery._get_version(path)
            if version:
                self.version_label.setText("Successfully initialized")
                self.version_label.setStyleSheet("color: green;")
            else:
                self.version_label.setText("Library found but could not initialize")
                self.version_label.setStyleSheet("color: orange;")
        else:
            self.status_label.setText("ERROR - libclang not found in this directory")
            self.status_label.setStyleSheet("color: red;")
            self.version_label.setText("-")
            self.version_label.setStyleSheet("")
    
    def _load_settings(self):
        """Load settings from JSON file."""
        if self.settings_path.exists():
            try:
                with open(self.settings_path, 'r', encoding='utf-8') as f:
                    settings = json.load(f)
                self.llvm_path_edit.setText(settings.get("llvm_bin_path", ""))
            except Exception:
                pass
    
    def _save_and_accept(self):
        """Save settings and close dialog."""
        path = self.llvm_path_edit.text().strip()
        
        # Validate before saving - LLVM is required
        if not path:
            QMessageBox.warning(
                self, "LLVM Required",
                "LLVM path is required for reflection generation.\n"
                "Please configure a valid LLVM path."
            )
            return
        
        discovery = LLVMDiscovery()
        if not discovery._validate_path(path):
            QMessageBox.warning(
                self, "Invalid Path",
                "The specified path does not contain libclang.\n"
                "Please specify a valid LLVM bin directory."
            )
            return
        
        # Load existing settings to preserve other values
        settings = {}
        if self.settings_path.exists():
            try:
                with open(self.settings_path, 'r', encoding='utf-8') as f:
                    settings = json.load(f)
            except Exception:
                pass
        
        # Update LLVM path
        settings["llvm_bin_path"] = path
        
        # Save settings
        try:
            with open(self.settings_path, 'w', encoding='utf-8') as f:
                json.dump(settings, f, indent=2)
        except Exception as e:
            QMessageBox.warning(self, "Save Error", f"Could not save settings: {e}")
            return
        
        self.accept()
