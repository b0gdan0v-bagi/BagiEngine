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
from .pipeline import Pipeline, PipelineExecutor, PipelineResult, MultiConfigPipelineExecutor, MultiConfigPipelineResult


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
        build_dir = get_build_dir_name(compiler)
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
            build_dir = get_build_dir_name(config.compiler)
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
            btn.clicked.connect(lambda: self._run_single_action("open_vs"))
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
        self._action_order = [
            "clean_build",
            "clean_all_builds",
            "cmake_configure",
            "cmake_build",
            "open_cursor",
            "open_vs",
            "open_xcode",
        ]
        
        self.step_checkboxes: dict[str, QCheckBox] = {}
        self._checkbox_base_names: dict[str, str] = {}  # Store base names for dynamic updates
        available = get_available_actions()
        
        for action_id in self._action_order:
            if action_id in available:
                action = available[action_id]
                checkbox = QCheckBox(action.name)
                checkbox.setProperty("action_id", action_id)
                self.step_checkboxes[action_id] = checkbox
                self._checkbox_base_names[action_id] = action.name
                layout.addWidget(checkbox)
        
        layout.addStretch()
        
        return group
    
    def _update_pipeline_labels(self):
        """Update checkbox labels with dynamic configuration info."""
        enabled_configs = self._get_enabled_configurations()
        active_build_dir = get_build_dir_name(self.config.compiler)
        
        # Get list of enabled build directories
        enabled_build_dirs = [get_build_dir_name(c.compiler) for c in enabled_configs]
        enabled_names = [c.name for c in enabled_configs]
        
        # Get VS generator configs for Open Visual Studio
        vs_configs = [c for c in enabled_configs if "Visual Studio" in c.generator]
        vs_build_dirs = [get_build_dir_name(c.compiler) for c in vs_configs]
        
        for action_id, checkbox in self.step_checkboxes.items():
            base_name = self._checkbox_base_names.get(action_id, checkbox.text())
            
            if action_id == "clean_build":
                # Show active build directory that will be cleaned
                checkbox.setText(f"{base_name}  →  build/{active_build_dir}")
            
            elif action_id == "clean_all_builds":
                # Show all enabled build directories
                if enabled_build_dirs:
                    dirs_str = ", ".join(f"build/{d}" for d in enabled_build_dirs)
                    checkbox.setText(f"{base_name}  →  {dirs_str}")
                else:
                    checkbox.setText(base_name)
            
            elif action_id in ("cmake_configure", "cmake_build"):
                # Show enabled configuration names
                if enabled_names:
                    names_str = ", ".join(enabled_names)
                    checkbox.setText(f"{base_name}  →  [{names_str}]")
                else:
                    checkbox.setText(f"{base_name}  →  (no configs enabled)")
            
            elif action_id == "open_vs":
                # Show VS generator build directories
                if vs_build_dirs:
                    dirs_str = ", ".join(f"build/{d}" for d in vs_build_dirs)
                    checkbox.setText(f"{base_name}  →  {dirs_str}")
                else:
                    checkbox.setText(f"{base_name}  →  (no VS configs)")
            
            elif action_id == "open_cursor":
                # Cursor opens workspace file, show that
                checkbox.setText(f"{base_name}  →  CI/BagiEngine.code-workspace")
            
            else:
                # Keep base name for other actions
                checkbox.setText(base_name)
    
    
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
        
        self._load_pipeline_preset(self.config.last_pipeline)
        self._update_pipeline_labels()
    
    def _load_pipeline_preset(self, preset_name: str):
        """Load a pipeline preset by checking the appropriate checkboxes."""
        # Uncheck all first
        for checkbox in self.step_checkboxes.values():
            checkbox.setChecked(False)
        
        # Check the ones in the preset
        pipelines = self.config.pipelines
        if preset_name in pipelines:
            for action_id in pipelines[preset_name]:
                if action_id in self.step_checkboxes:
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
        # Iterate in the defined order to maintain execution sequence
        for action_id in self._action_order:
            if action_id in self.step_checkboxes:
                if self.step_checkboxes[action_id].isChecked():
                    steps.append(action_id)
        
        return Pipeline("custom", steps)
    
    def _run_single_action(self, action_id: str):
        """Run a single action."""
        pipeline = Pipeline("single", [action_id])
        self._execute_pipeline(pipeline)
    
    def _open_settings(self):
        """Open the settings dialog."""
        dialog = SettingsDialog(self, self.config)
        dialog.exec()
        # Update labels after settings may have changed
        self._update_pipeline_labels()
    
    def _clean_build(self):
        """Clean build directory for current compiler configuration."""
        compiler = self.config.compiler
        build_dir = get_build_dir_name(compiler)
        
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
        vs_build_dirs = [get_build_dir_name(c.compiler) for c in vs_configs] if vs_configs else None
        
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
