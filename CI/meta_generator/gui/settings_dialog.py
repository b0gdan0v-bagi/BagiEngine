"""
Settings Dialog for Meta-Generator

The reflection parser is now regex-based and requires no external configuration.
This dialog is kept as a minimal placeholder for future settings.
"""

from pathlib import Path

from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QLabel, QDialogButtonBox
)


class SettingsDialog(QDialog):
    """Settings dialog for Meta-Generator."""

    def __init__(self, parent=None, project_root: Path = None):
        super().__init__(parent)
        self.project_root = project_root or Path.cwd()
        self.setWindowTitle("Meta-Generator Settings")
        self.setMinimumWidth(400)
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        info = QLabel(
            "The reflection parser uses a fast regex-based approach.\n"
            "No external dependencies (LLVM) are required."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        layout.addStretch()

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self.accept)
        layout.addWidget(buttons)
