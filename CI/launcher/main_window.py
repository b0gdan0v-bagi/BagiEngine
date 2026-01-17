"""Main window for the BagiEngine Launcher."""

from pathlib import Path
from typing import Optional, Union

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGroupBox, QPushButton, QComboBox, QLabel,
    QTextEdit, QListWidget, QListWidgetItem, QProgressBar,
    QSplitter, QFrame, QMessageBox, QCheckBox, QDialog,
    QFormLayout, QLineEdit, QDialogButtonBox
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QFont

from .config import Config, BuildConfiguration
from .actions import ACTIONS, get_available_actions, ActionResult, get_current_platform, Platform, get_build_dir_name
from .pipeline import Pipeline, PipelineStep, PipelineExecutor, PipelineResult, MultiConfigPipelineExecutor, MultiConfigPipelineResult


class PipelineWorker(QThread):
    """Worker thread for pipeline execution."""
    
    step_started = pyqtSignal(int, str)
    step_completed = pyqtSignal(int, str, object)
    output_received = pyqtSignal(str)
    finished_signal = pyqtSignal(object)
    
    def __init__(self, executor: PipelineExecutor, pipeline: Pipeline,
                 build_type: str, generator: str, compiler: str, ide: str,
                 vs_build_dirs: list[str] | None = None):
        super().__init__()
        self.executor = executor
        self.pipeline = pipeline
        self.build_type = build_type
        self.generator = generator
        self.compiler = compiler
        self.ide = ide
        self.vs_build_dirs = vs_build_dirs
    
    def run(self):
        result = self.executor.execute(
            self.pipeline,
            build_type=self.build_type,
            generator=self.generator,
            compiler=self.compiler,
            ide=self.ide,
            vs_build_dirs=self.vs_build_dirs,
            on_step_start=lambda i, name: self.step_started.emit(i, name),
            on_step_complete=lambda i, name, r: self.step_completed.emit(i, name, r),
            on_output=lambda out: self.output_received.emit(out),
        )
        self.finished_signal.emit(result)


class MultiConfigPipelineWorker(QThread):
    """Worker thread for multi-configuration pipeline execution."""
    
    config_started = pyqtSignal(int, str, str)  # index, name, build_dir
    config_completed = pyqtSignal(int, str, bool)  # index, name, success
    step_started = pyqtSignal(int, str)
    step_completed = pyqtSignal(int, str, object)
    output_received = pyqtSignal(str)
    finished_signal = pyqtSignal(object)
    
    def __init__(self, executor: MultiConfigPipelineExecutor, pipeline: Pipeline,
                 configurations: list[BuildConfiguration], ide: str):
        super().__init__()
        self.executor = executor
        self.pipeline = pipeline
        self.configurations = configurations
        self.ide = ide
    
    def run(self):
        result = self.executor.execute(
            self.pipeline,
            configurations=self.configurations,
            ide=self.ide,
            on_config_start=lambda i, name, bd: self.config_started.emit(i, name, bd),
            on_config_complete=lambda i, name, s: self.config_completed.emit(i, name, s),
            on_step_start=lambda i, name: self.step_started.emit(i, name),
            on_step_complete=lambda i, name, r: self.step_completed.emit(i, name, r),
            on_output=lambda out: self.output_received.emit(out),
        )
        self.finished_signal.emit(result)


class ConfigurationDialog(QDialog):
    """Dialog for creating/editing build configurations."""
    
    def __init__(self, parent=None, config: Optional[BuildConfiguration] = None):
        super().__init__(parent)
        self.setWindowTitle("Edit Configuration" if config else "New Configuration")
        self.setMinimumWidth(400)
        
        self._config = config
        self._setup_ui()
        
        if config:
            self._load_config(config)
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        # Form layout
        form_layout = QFormLayout()
        
        # Name
        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText("e.g., MSVC Debug")
        form_layout.addRow("Name:", self.name_edit)
        
        # Compiler
        self.compiler_combo = QComboBox()
        self.compiler_combo.addItems(["MSVC", "Clang", "GCC (MinGW)"])
        self.compiler_combo.currentTextChanged.connect(self._update_preview)
        form_layout.addRow("Compiler:", self.compiler_combo)
        
        # Generator
        self.generator_combo = QComboBox()
        generators = ["Ninja", "Ninja Multi-Config", "Unix Makefiles"]
        if get_current_platform() == Platform.WINDOWS:
            generators.extend(["Visual Studio 17 2022", "Visual Studio 16 2019", "NMake Makefiles"])
        elif get_current_platform() == Platform.MACOS:
            generators.append("Xcode")
        self.generator_combo.addItems(generators)
        form_layout.addRow("Generator:", self.generator_combo)
        
        # Build Type
        self.build_type_combo = QComboBox()
        self.build_type_combo.addItems(["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])
        self.build_type_combo.currentTextChanged.connect(self._update_preview)
        form_layout.addRow("Build Type:", self.build_type_combo)
        
        layout.addLayout(form_layout)
        
        # Preview label
        self.preview_label = QLabel()
        self.preview_label.setStyleSheet("color: gray; font-style: italic;")
        self._update_preview()
        layout.addWidget(self.preview_label)
        
        # Buttons
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._validate_and_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
    
    def _update_preview(self):
        """Update the build directory preview."""
        compiler = self.compiler_combo.currentText()
        build_type = self.build_type_combo.currentText()
        build_dir = get_build_dir_name(compiler, with_solution_suffix=True)
        self.preview_label.setText(f"Build directory: build/{build_dir}")
    
    def _load_config(self, config: BuildConfiguration):
        """Load configuration into the dialog."""
        self.name_edit.setText(config.name)
        
        idx = self.compiler_combo.findText(config.compiler)
        if idx >= 0:
            self.compiler_combo.setCurrentIndex(idx)
        
        idx = self.generator_combo.findText(config.generator)
        if idx >= 0:
            self.generator_combo.setCurrentIndex(idx)
        
        idx = self.build_type_combo.findText(config.build_type)
        if idx >= 0:
            self.build_type_combo.setCurrentIndex(idx)
    
    def _validate_and_accept(self):
        """Validate input and accept dialog."""
        name = self.name_edit.text().strip()
        if not name:
            QMessageBox.warning(self, "Validation Error", "Please enter a configuration name.")
            return
        self.accept()
    
    def get_configuration(self) -> BuildConfiguration:
        """Get the configured BuildConfiguration."""
        return BuildConfiguration(
            name=self.name_edit.text().strip(),
            compiler=self.compiler_combo.currentText(),
            generator=self.generator_combo.currentText(),
            build_type=self.build_type_combo.currentText(),
            enabled=self._config.enabled if self._config else True
        )


class VSConfigurationDialog(QDialog):
    """Dialog for selecting Visual Studio configuration to open."""
    
    def __init__(self, parent=None, configurations: list[BuildConfiguration] = None):
        super().__init__(parent)
        self.setWindowTitle("Select Visual Studio Configuration")
        self.setMinimumWidth(400)
        
        self._configurations = configurations or []
        self._selected_config: Optional[BuildConfiguration] = None
        self._setup_ui()
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        if not self._configurations:
            label = QLabel("No Visual Studio configurations found.")
            layout.addWidget(label)
            buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok)
            buttons.accepted.connect(self.reject)
            layout.addWidget(buttons)
            return
        
        label = QLabel("Select configuration to open in Visual Studio:")
        layout.addWidget(label)
        
        self.config_list = QListWidget()
        for config in self._configurations:
            build_dir = get_build_dir_name(config.compiler, with_solution_suffix=True)
            item_text = f"{config.name} (build/{build_dir})"
            item = QListWidgetItem(item_text)
            item.setData(Qt.ItemDataRole.UserRole, config)
            self.config_list.addItem(item)
        
        self.config_list.setCurrentRow(0)
        layout.addWidget(self.config_list)
        
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._accept_selection)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
    
    def _accept_selection(self):
        """Accept the selected configuration."""
        current_item = self.config_list.currentItem()
        if current_item:
            self._selected_config = current_item.data(Qt.ItemDataRole.UserRole)
            self.accept()
        else:
            self.reject()
    
    def get_selected_configuration(self) -> Optional[BuildConfiguration]:
        """Get the selected configuration."""
        return self._selected_config


class SettingsDialog(QDialog):
    """Dialog for managing build configurations.
    
    Uses a single unified list of configurations where one is marked as 'active'
    for single runs, and multiple can be enabled for batch builds.
    """
    
    def __init__(self, parent=None, config: 'Config' = None):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setMinimumWidth(550)
        self.setMinimumHeight(400)
        
        self._config = config
        self._setup_ui()
        self._load_settings()
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        # Build Configurations group (unified - replaces separate Build Settings)
        configs_group = QGroupBox("Build Configurations")
        configs_layout = QVBoxLayout(configs_group)
        
        # Help text
        help_label = QLabel(
            "• <b>Active</b> (●) — used for single builds and Quick Actions\n"
            "• <b>Enabled</b> (☑) — included in batch builds"
        )
        help_label.setWordWrap(True)
        help_label.setStyleSheet("color: gray; font-size: 11px; margin-bottom: 8px;")
        configs_layout.addWidget(help_label)
        
        # Configurations list
        self.configs_list = QListWidget()
        self.configs_list.setMinimumHeight(200)
        self.configs_list.itemDoubleClicked.connect(self._edit_configuration)
        configs_layout.addWidget(self.configs_list)
        
        # Configuration controls
        config_buttons = QHBoxLayout()
        
        add_config_btn = QPushButton("Add")
        add_config_btn.clicked.connect(self._add_configuration)
        config_buttons.addWidget(add_config_btn)
        
        edit_config_btn = QPushButton("Edit")
        edit_config_btn.clicked.connect(self._edit_configuration)
        config_buttons.addWidget(edit_config_btn)
        
        remove_config_btn = QPushButton("Remove")
        remove_config_btn.clicked.connect(self._remove_configuration)
        config_buttons.addWidget(remove_config_btn)
        
        config_buttons.addStretch()
        
        set_active_btn = QPushButton("Set as Active")
        set_active_btn.setToolTip("Use this configuration for single builds")
        set_active_btn.clicked.connect(self._set_active_configuration)
        config_buttons.addWidget(set_active_btn)
        
        configs_layout.addLayout(config_buttons)
        layout.addWidget(configs_group)
        
        # Buttons
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save_and_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
    
    def _load_settings(self):
        """Load settings from config into UI."""
        if not self._config:
            return
        self._load_configurations()
    
    def _get_active_config_name(self) -> str:
        """Get a unique identifier for the currently active configuration."""
        return f"{self._config.compiler}|{self._config.generator}|{self._config.build_type}"
    
    def _load_configurations(self):
        """Load build configurations into the list."""
        self.configs_list.clear()
        
        active_key = self._get_active_config_name()
        
        for config in self._config.build_configurations:
            item = QListWidgetItem()
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked if config.enabled else Qt.CheckState.Unchecked)
            
            # Check if this is the active configuration
            config_key = f"{config.compiler}|{config.generator}|{config.build_type}"
            is_active = (config_key == active_key)
            
            # Display format: "● Name (Compiler/Generator/BuildType) -> build/Dir"
            build_dir = get_build_dir_name(config.compiler, with_solution_suffix=True)
            active_marker = "● " if is_active else "  "
            item.setText(f"{active_marker}{config.name} ({config.compiler}/{config.generator}/{config.build_type}) -> build/{build_dir}")
            item.setData(Qt.ItemDataRole.UserRole, config)
            
            if is_active:
                font = item.font()
                font.setBold(True)
                item.setFont(font)
            
            self.configs_list.addItem(item)
    
    def _add_configuration(self):
        """Add a new build configuration."""
        dialog = ConfigurationDialog(self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            config = dialog.get_configuration()
            configs = self._config.build_configurations
            configs.append(config)
            self._config.build_configurations = configs
            self._load_configurations()
    
    def _edit_configuration(self):
        """Edit selected build configuration."""
        current_row = self.configs_list.currentRow()
        if current_row < 0:
            QMessageBox.warning(self, "No Selection", "Please select a configuration to edit.")
            return
        
        item = self.configs_list.item(current_row)
        config: BuildConfiguration = item.data(Qt.ItemDataRole.UserRole)
        
        dialog = ConfigurationDialog(self, config)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            new_config = dialog.get_configuration()
            configs = self._config.build_configurations
            configs[current_row] = new_config
            self._config.build_configurations = configs
            self._load_configurations()
    
    def _remove_configuration(self):
        """Remove selected build configuration."""
        current_row = self.configs_list.currentRow()
        if current_row < 0:
            QMessageBox.warning(self, "No Selection", "Please select a configuration to remove.")
            return
        
        configs = self._config.build_configurations
        if len(configs) <= 1:
            QMessageBox.warning(self, "Cannot Remove", "You must have at least one configuration.")
            return
        
        del configs[current_row]
        self._config.build_configurations = configs
        self._load_configurations()
    
    def _set_active_configuration(self):
        """Set selected configuration as the active one for single builds."""
        current_row = self.configs_list.currentRow()
        if current_row < 0:
            QMessageBox.warning(self, "No Selection", "Please select a configuration to set as active.")
            return
        
        item = self.configs_list.item(current_row)
        config: BuildConfiguration = item.data(Qt.ItemDataRole.UserRole)
        
        # Update the main config settings to match this configuration
        self._config.compiler = config.compiler
        self._config.generator = config.generator
        self._config.build_type = config.build_type
        
        # Refresh the list to show the new active marker
        self._load_configurations()
        self.configs_list.setCurrentRow(current_row)
    
    def _save_and_accept(self):
        """Save settings and close dialog."""
        # Save configuration enabled states
        configs = []
        for i in range(self.configs_list.count()):
            item = self.configs_list.item(i)
            config: BuildConfiguration = item.data(Qt.ItemDataRole.UserRole)
            config.enabled = item.checkState() == Qt.CheckState.Checked
            configs.append(config)
        
        self._config.build_configurations = configs
        
        self.accept()


class MainWindow(QMainWindow):
    """Main application window."""
    
    def __init__(self, project_root: Path):
        super().__init__()
        self.project_root = project_root
        self.config = Config(project_root / "launcher_config.json")
        self.executor = PipelineExecutor(project_root)
        self.multi_executor = MultiConfigPipelineExecutor(project_root)
        self.worker: Optional[Union[PipelineWorker, MultiConfigPipelineWorker]] = None
        
        self.setWindowTitle("BagiEngine Launcher")
        self.setMinimumSize(900, 700)
        
        self._setup_ui()
        self._load_config()
    
    def _setup_ui(self):
        """Setup the user interface."""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        main_layout = QVBoxLayout(central_widget)
        
        # Create splitter for resizable sections
        splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # Left panel - Actions and Pipeline
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setContentsMargins(0, 0, 0, 0)
        
        # Quick Actions group
        actions_group = self._create_actions_group()
        left_layout.addWidget(actions_group)
        
        # Pipeline Builder group
        pipeline_group = self._create_pipeline_group()
        left_layout.addWidget(pipeline_group)
        
        splitter.addWidget(left_panel)
        
        # Right panel - Output Log
        output_group = self._create_output_group()
        splitter.addWidget(output_group)
        
        splitter.setSizes([350, 450])
        main_layout.addWidget(splitter)
        
        # Bottom - Run controls
        run_layout = QHBoxLayout()
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        run_layout.addWidget(self.progress_bar)
        
        self.run_button = QPushButton("Run Pipeline")
        self.run_button.setMinimumHeight(40)
        self.run_button.setStyleSheet("font-weight: bold; font-size: 14px;")
        self.run_button.clicked.connect(self._run_pipeline)
        run_layout.addWidget(self.run_button)
        
        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.setMinimumHeight(40)
        self.cancel_button.setEnabled(False)
        self.cancel_button.clicked.connect(self._cancel_pipeline)
        run_layout.addWidget(self.cancel_button)
        
        main_layout.addLayout(run_layout)
    
    def _create_actions_group(self) -> QGroupBox:
        """Create quick actions group."""
        group = QGroupBox("Quick Actions")
        layout = QHBoxLayout(group)
        
        available = get_available_actions()
        
        # CMake Configure button
        if "cmake_configure" in available:
            btn = QPushButton("CMake Configure")
            btn.clicked.connect(lambda: self._run_single_action("cmake_configure"))
            layout.addWidget(btn)
        
        # CMake Build button
        if "cmake_build" in available:
            btn = QPushButton("CMake Build")
            btn.clicked.connect(lambda: self._run_single_action("cmake_build"))
            layout.addWidget(btn)
        
        # Clean button
        if "clean_build" in available:
            btn = QPushButton("Clean")
            btn.setToolTip("Clean current build directory (permanent deletion via git clean)")
            btn.clicked.connect(self._clean_build)
            layout.addWidget(btn)
        
        # Open Cursor button
        if "open_cursor" in available:
            btn = QPushButton("Open Cursor")
            btn.clicked.connect(lambda: self._run_single_action("open_cursor"))
            layout.addWidget(btn)
        
        # Open Visual Studio button (Windows only)
        if "open_vs" in available:
            btn = QPushButton("Open Visual Studio")
            btn.clicked.connect(self._open_visual_studio_with_selection)
            layout.addWidget(btn)
        
        # Settings button
        btn = QPushButton("Settings")
        btn.clicked.connect(self._open_settings)
        layout.addWidget(btn)
        
        return group
    
    def _create_pipeline_group(self) -> QGroupBox:
        """Create pipeline builder group with checkboxes for each action."""
        group = QGroupBox("Pipeline Builder")
        layout = QVBoxLayout(group)
        
        # Preset selector
        preset_layout = QHBoxLayout()
        preset_layout.addWidget(QLabel("Preset:"))
        
        self.preset_combo = QComboBox()
        for name in self.config.pipelines.keys():
            self.preset_combo.addItem(name)
        self.preset_combo.currentTextChanged.connect(self._load_pipeline_preset)
        preset_layout.addWidget(self.preset_combo, 1)
        
        layout.addLayout(preset_layout)
        
        # Pipeline steps as checkboxes (fixed order for better UX)
        layout.addWidget(QLabel("Steps (in execution order):"))
        
        # Define action order explicitly for logical grouping
        # Actions that need per-config checkboxes: clean_build, cmake_configure, cmake_build
        # Actions that are static: close_vs (before clean), open_cursor, open_vs, open_xcode
        self._config_actions = ["clean_build", "cmake_configure", "cmake_build"]
        self._static_actions = ["open_cursor", "open_vs", "open_xcode"]
        self._pre_clean_actions = ["close_vs"]  # Actions that go before clean operations
        
        self.step_checkboxes: dict[str, QCheckBox] = {}
        self._pipeline_layout = layout  # Store layout reference for dynamic updates
        available = get_available_actions()
        
        # Create pre-clean static checkboxes first (close_vs before clean)
        for action_id in self._pre_clean_actions:
            if action_id in available:
                action = available[action_id]
                checkbox = QCheckBox(action.name)
                checkbox.setProperty("action_id", action_id)
                checkbox.stateChanged.connect(self._on_checkbox_changed)
                self.step_checkboxes[action_id] = checkbox
                layout.addWidget(checkbox)
        
        # Create dynamic config checkboxes (clean_build, cmake_configure, cmake_build)
        self._rebuild_config_checkboxes()
        
        # Create other static checkboxes after config actions
        for action_id in self._static_actions:
            if action_id in available:
                action = available[action_id]
                checkbox = QCheckBox(action.name)
                checkbox.setProperty("action_id", action_id)
                checkbox.stateChanged.connect(self._on_checkbox_changed)
                self.step_checkboxes[action_id] = checkbox
                layout.addWidget(checkbox)
        
        layout.addStretch()
        
        return group
    
    def _rebuild_config_checkboxes(self):
        """Rebuild checkboxes for configuration-specific actions."""
        # Check if layout is initialized
        if not hasattr(self, '_pipeline_layout') or self._pipeline_layout is None:
            return
        
        # Remove existing config checkboxes
        config_checkbox_keys = [
            key for key in self.step_checkboxes.keys()
            if any(key.startswith(f"{action_id}:") for action_id in self._config_actions)
        ]
        
        for key in config_checkbox_keys:
            checkbox = self.step_checkboxes.pop(key)
            # Find and remove from layout
            self._pipeline_layout.removeWidget(checkbox)
            checkbox.deleteLater()
        
        # Get enabled configurations
        enabled_configs = self._get_enabled_configurations()
        available = get_available_actions()
        
        # Find insertion point (after pre-clean actions like close_vs, before other static actions)
        insertion_index = None
        # Find the last pre-clean action (close_vs)
        for i in range(self._pipeline_layout.count()):
            item = self._pipeline_layout.itemAt(i)
            if item and item.widget():
                widget = item.widget()
                if hasattr(widget, 'property') and widget.property("action_id") in self._pre_clean_actions:
                    insertion_index = i + 1  # Insert after this action
        # If no pre-clean actions, find first static action
        if insertion_index is None:
            for i in range(self._pipeline_layout.count()):
                item = self._pipeline_layout.itemAt(i)
                if item and item.widget():
                    widget = item.widget()
                    if hasattr(widget, 'property') and widget.property("action_id") in self._static_actions:
                        insertion_index = i
                        break
        
        if insertion_index is None:
            insertion_index = self._pipeline_layout.count() - 1  # Before stretch
        
        # Create checkboxes for each config action and each enabled config
        # Order: clean_build for all configs, then cmake_configure for all configs, then cmake_build for all configs
        action_display_names = {
            "clean_build": "Clean Build",
            "cmake_configure": "CMake Configure",
            "cmake_build": "CMake Build"
        }
        
        for action_id in self._config_actions:
            if action_id not in available:
                continue
            
            for config in enabled_configs:
                checkbox_key = f"{action_id}:{config.name}"
                display_name = f"{action_display_names[action_id]} {config.name}"
                
                checkbox = QCheckBox(display_name)
                checkbox.setProperty("action_id", action_id)
                checkbox.setProperty("config_name", config.name)
                checkbox.stateChanged.connect(self._on_checkbox_changed)
                self.step_checkboxes[checkbox_key] = checkbox
                self._pipeline_layout.insertWidget(insertion_index, checkbox)
                insertion_index += 1
    
    
    def _create_output_group(self) -> QGroupBox:
        """Create output log group."""
        group = QGroupBox("Output")
        layout = QVBoxLayout(group)
        
        self.output_text = QTextEdit()
        self.output_text.setReadOnly(True)
        self.output_text.setFont(QFont("Consolas", 9))
        layout.addWidget(self.output_text)
        
        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self.output_text.clear)
        layout.addWidget(clear_btn)
        
        return group
    
    def _load_config(self):
        """Load configuration into UI."""
        # Pipeline preset
        idx = self.preset_combo.findText(self.config.last_pipeline)
        if idx >= 0:
            self.preset_combo.setCurrentIndex(idx)
        
        # Don't load preset automatically - load saved checkbox states instead
        self._rebuild_config_checkboxes()
        self._load_checkbox_states()
    
    def _on_checkbox_changed(self):
        """Handle checkbox state change - save to config immediately."""
        self._save_checkbox_states()
    
    def _save_checkbox_states(self):
        """Save current checkbox states to config."""
        states = {}
        for checkbox_key, checkbox in self.step_checkboxes.items():
            states[checkbox_key] = checkbox.isChecked()
        self.config.pipeline_step_states = states
    
    def _load_checkbox_states(self):
        """Load checkbox states from config."""
        states = self.config.pipeline_step_states
        for checkbox_key, checkbox in self.step_checkboxes.items():
            if checkbox_key in states:
                checkbox.setChecked(states[checkbox_key])
    
    def _load_pipeline_preset(self, preset_name: str):
        """Load a pipeline preset by checking the appropriate checkboxes."""
        # Uncheck all first
        for checkbox in self.step_checkboxes.values():
            checkbox.setChecked(False)
        
        # Check the ones in the preset
        pipelines = self.config.pipelines
        if preset_name in pipelines:
            enabled_configs = self._get_enabled_configurations()
            for action_id in pipelines[preset_name]:
                # For config actions, check all enabled configs
                if action_id in self._config_actions:
                    for config in enabled_configs:
                        checkbox_key = f"{action_id}:{config.name}"
                        if checkbox_key in self.step_checkboxes:
                            self.step_checkboxes[checkbox_key].setChecked(True)
                # For static actions, check directly
                elif action_id in self.step_checkboxes:
                    self.step_checkboxes[action_id].setChecked(True)
        
        self.config.last_pipeline = preset_name
    
    def _get_enabled_configurations(self) -> list[BuildConfiguration]:
        """Get list of enabled configurations from config."""
        return [c for c in self.config.build_configurations if c.enabled]
    
    def _run_all_configurations(self):
        """Run pipeline for all enabled configurations."""
        pipeline = self._get_current_pipeline()
        if not pipeline.steps:
            QMessageBox.warning(self, "No Steps", "Please add at least one step to the pipeline.")
            return
        
        configs = self._get_enabled_configurations()
        if not configs:
            QMessageBox.warning(self, "No Configurations", "Please enable at least one build configuration in Settings.")
            return
        
        self._execute_multi_config_pipeline(pipeline, configs)
    
    def _get_current_pipeline(self) -> Pipeline:
        """Get current pipeline from checked checkboxes."""
        steps = []
        
        # Group checked config actions by configuration
        config_actions_by_config: dict[str, list[str]] = {}
        checked_static_actions = []
        
        for checkbox_key, checkbox in self.step_checkboxes.items():
            if not checkbox.isChecked():
                continue
            
            # Check if this is a config action (has ":")
            if ":" in checkbox_key:
                action_id, config_name = checkbox_key.split(":", 1)
                if config_name not in config_actions_by_config:
                    config_actions_by_config[config_name] = []
                config_actions_by_config[config_name].append(action_id)
            else:
                # Static action
                action_id = checkbox_key
                checked_static_actions.append(action_id)
        
        # Build pipeline steps in order: pre-clean actions, then clean_build, cmake_configure, cmake_build, then other static actions
        # First, add pre-clean actions if checked (before clean operations)
        for action_id in self._pre_clean_actions:
            if action_id in checked_static_actions:
                steps.append(PipelineStep(action_id=action_id, params={}))
        
        # For each configuration, add its actions in order
        enabled_configs = self._get_enabled_configurations()
        config_names = [c.name for c in enabled_configs]
        
        # Process configs in order
        for config_name in config_names:
            if config_name in config_actions_by_config:
                actions = config_actions_by_config[config_name]
                # Add in order: clean_build, cmake_configure, cmake_build
                for action_id in ["clean_build", "cmake_configure", "cmake_build"]:
                    if action_id in actions:
                        # Find the config object
                        config = next((c for c in enabled_configs if c.name == config_name), None)
                        if config:
                            # Create step with config params
                            step = PipelineStep(
                                action_id=action_id,
                                params={
                                    "compiler": config.compiler,
                                    "generator": config.generator,
                                    "build_type": config.build_type
                                }
                            )
                            steps.append(step)
        
        # Add other static actions
        for action_id in self._static_actions:
            if action_id in checked_static_actions:
                # For open_vs, determine VS configurations
                if action_id == "open_vs":
                    vs_configs = [c for c in enabled_configs if "Visual Studio" in c.generator]
                    if vs_configs:
                        vs_build_dirs = [get_build_dir_name(c.compiler, with_solution_suffix=True) for c in vs_configs]
                        steps.append(PipelineStep(action_id=action_id, params={"vs_build_dirs": vs_build_dirs}))
                    else:
                        # No VS configs, skip this action
                        pass
                else:
                    steps.append(PipelineStep(action_id=action_id, params={}))
        
        # Create pipeline with empty steps, then set steps directly
        pipeline = Pipeline("custom", [])
        pipeline._steps = steps
        return pipeline
    
    def _run_single_action(self, action_id: str):
        """Run a single action."""
        pipeline = Pipeline("single", [action_id])
        self._execute_pipeline(pipeline)
    
    def _open_visual_studio_with_selection(self):
        """Open Visual Studio with configuration selection dialog."""
        # Get VS generator configurations
        enabled_configs = self._get_enabled_configurations()
        vs_configs = [c for c in enabled_configs if "Visual Studio" in c.generator]
        
        if not vs_configs:
            QMessageBox.warning(
                self,
                "No Visual Studio Configurations",
                "No Visual Studio generator configurations found.\n"
                "Please configure at least one build configuration with Visual Studio generator in Settings."
            )
            return
        
        # If only one VS config, open it directly
        if len(vs_configs) == 1:
            config = vs_configs[0]
            build_dir = get_build_dir_name(config.compiler, with_solution_suffix=True)
            pipeline = Pipeline("open_vs", ["open_vs"])
            # Create step with config params
            step = PipelineStep(
                action_id="open_vs",
                params={"vs_build_dirs": [build_dir]}
            )
            pipeline._steps = [step]
            self._execute_pipeline(pipeline)
        else:
            # Show selection dialog
            dialog = VSConfigurationDialog(self, vs_configs)
            if dialog.exec() == QDialog.DialogCode.Accepted:
                selected_config = dialog.get_selected_configuration()
                if selected_config:
                    build_dir = get_build_dir_name(selected_config.compiler, with_solution_suffix=True)
                    pipeline = Pipeline("open_vs", ["open_vs"])
                    step = PipelineStep(
                        action_id="open_vs",
                        params={"vs_build_dirs": [build_dir]}
                    )
                    pipeline._steps = [step]
                    self._execute_pipeline(pipeline)
    
    def _open_settings(self):
        """Open the settings dialog."""
        dialog = SettingsDialog(self, self.config)
        dialog.exec()
        # Rebuild config checkboxes after settings may have changed
        self._rebuild_config_checkboxes()
        # Restore checkbox states after rebuilding
        self._load_checkbox_states()
    
    def _clean_build(self):
        """Clean build directory for current compiler configuration."""
        compiler = self.config.compiler
        build_type = self.config.build_type
        build_dir = get_build_dir_name(compiler, with_solution_suffix=True)
        
        reply = QMessageBox.question(
            self,
            "Confirm Clean",
            f"This will permanently delete all files in build/{build_dir}.\n\n"
            "This action cannot be undone (files will NOT go to Recycle Bin).\n\n"
            "Continue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            pipeline = Pipeline("clean", ["clean_build"])
            self._execute_pipeline(pipeline)
    
    def _run_pipeline(self):
        """Run the current pipeline."""
        pipeline = self._get_current_pipeline()
        if not pipeline.steps:
            QMessageBox.warning(self, "No Steps", "Please add at least one step to the pipeline.")
            return
        
        self._execute_pipeline(pipeline)
    
    def _execute_pipeline(self, pipeline: Pipeline):
        """Execute a pipeline in a worker thread."""
        self.run_button.setEnabled(False)
        self.cancel_button.setEnabled(True)
        self.progress_bar.setVisible(True)
        self.progress_bar.setMaximum(len(pipeline.steps))
        self.progress_bar.setValue(0)
        
        self._log(f"Starting pipeline with {len(pipeline.steps)} step(s)...")
        
        # Get VS build directories for open_vs action
        enabled_configs = self._get_enabled_configurations()
        vs_configs = [c for c in enabled_configs if "Visual Studio" in c.generator]
        vs_build_dirs = [get_build_dir_name(c.compiler, with_solution_suffix=True) for c in vs_configs] if vs_configs else None
        
        # Use config defaults, but step.params will override them in PipelineExecutor
        self.worker = PipelineWorker(
            self.executor,
            pipeline,
            self.config.build_type,
            self.config.generator,
            self.config.compiler,
            self.config.last_ide,
            vs_build_dirs
        )
        
        self.worker.step_started.connect(self._on_step_started)
        self.worker.step_completed.connect(self._on_step_completed)
        self.worker.output_received.connect(self._log)
        self.worker.finished_signal.connect(self._on_pipeline_finished)
        
        self.worker.start()
    
    def _execute_multi_config_pipeline(self, pipeline: Pipeline, configurations: list[BuildConfiguration]):
        """Execute a pipeline for multiple configurations."""
        self.run_button.setEnabled(False)
        self.cancel_button.setEnabled(True)
        self.progress_bar.setVisible(True)
        
        # Total steps = steps per config * number of configs
        total_steps = len(pipeline.steps) * len(configurations)
        self.progress_bar.setMaximum(total_steps)
        self.progress_bar.setValue(0)
        
        self._log(f"Starting multi-config pipeline with {len(configurations)} configuration(s)...")
        self._current_config_index = 0
        self._steps_per_config = len(pipeline.steps)
        
        self.worker = MultiConfigPipelineWorker(
            self.multi_executor,
            pipeline,
            configurations,
            self.config.last_ide
        )
        
        self.worker.config_started.connect(self._on_config_started)
        self.worker.config_completed.connect(self._on_config_completed)
        self.worker.step_started.connect(self._on_multi_step_started)
        self.worker.step_completed.connect(self._on_multi_step_completed)
        self.worker.output_received.connect(self._log)
        self.worker.finished_signal.connect(self._on_multi_config_pipeline_finished)
        
        self.worker.start()
    
    def _on_config_started(self, index: int, name: str, build_dir: str):
        """Handle configuration started."""
        self._current_config_index = index
    
    def _on_config_completed(self, index: int, name: str, success: bool):
        """Handle configuration completed."""
        status = "SUCCESS" if success else "FAILED"
        self._log(f"\nConfiguration '{name}': {status}\n")
    
    def _on_multi_step_started(self, index: int, name: str):
        """Handle step started in multi-config mode."""
        pass  # Output is handled by the executor
    
    def _on_multi_step_completed(self, index: int, name: str, result: ActionResult):
        """Handle step completed in multi-config mode."""
        # Update progress bar
        current_value = self._current_config_index * self._steps_per_config + index + 1
        self.progress_bar.setValue(current_value)
    
    def _on_multi_config_pipeline_finished(self, result: MultiConfigPipelineResult):
        """Handle multi-config pipeline finished."""
        self.run_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.progress_bar.setVisible(False)
        
        self._log(f"\n{'='*60}")
        self._log(result.summary)
        self._log(f"{'='*60}")
        
        # Show detailed results
        for config_result in result.results:
            status = "OK" if config_result.success else "FAILED"
            self._log(f"  - {config_result.config_name} (build/{config_result.build_dir}): {status}")
    
    def _cancel_pipeline(self):
        """Cancel the running pipeline."""
        if self.worker:
            self.executor.cancel()
            self.multi_executor.cancel()
            self._log("Pipeline cancelled.")
    
    def _on_step_started(self, index: int, name: str):
        """Handle step started."""
        self._log(f"[{index + 1}] Starting: {name}")
    
    def _on_step_completed(self, index: int, name: str, result: ActionResult):
        """Handle step completed."""
        self.progress_bar.setValue(index + 1)
        status = "OK" if result.success else "FAILED"
        self._log(f"[{index + 1}] {name}: {status}")
    
    def _on_pipeline_finished(self, result: PipelineResult):
        """Handle pipeline finished."""
        self.run_button.setEnabled(True)
        self.cancel_button.setEnabled(False)
        self.progress_bar.setVisible(False)
        
        self._log(f"\n{result.summary}")
        
        if result.success:
            self._log("Pipeline completed successfully!")
        else:
            self._log("Pipeline failed. Check output for details.")
    
    def _log(self, message: str):
        """Append message to output log."""
        self.output_text.append(message)
        # Scroll to bottom
        scrollbar = self.output_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def closeEvent(self, event):
        """Handle window close."""
        if self.worker and self.worker.isRunning():
            self.executor.cancel()
            self.multi_executor.cancel()
            self.worker.wait()
        event.accept()
