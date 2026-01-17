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
                 build_type: str, generator: str, compiler: str, ide: str):
        super().__init__()
        self.executor = executor
        self.pipeline = pipeline
        self.build_type = build_type
        self.generator = generator
        self.compiler = compiler
        self.ide = ide
    
    def run(self):
        result = self.executor.execute(
            self.pipeline,
            build_type=self.build_type,
            generator=self.generator,
            compiler=self.compiler,
            ide=self.ide,
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
    """Dialog for managing build settings and configurations."""
    
    def __init__(self, parent=None, config: 'Config' = None):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setMinimumWidth(500)
        self.setMinimumHeight(400)
        
        self._config = config
        self._setup_ui()
        self._load_settings()
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        # Build Settings group
        settings_group = QGroupBox("Build Settings")
        settings_layout = QVBoxLayout(settings_group)
        
        # Build type
        build_layout = QHBoxLayout()
        build_layout.addWidget(QLabel("Build Type:"))
        self.build_type_combo = QComboBox()
        self.build_type_combo.addItems(["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])
        build_layout.addWidget(self.build_type_combo, 1)
        settings_layout.addLayout(build_layout)
        
        # Generator
        gen_layout = QHBoxLayout()
        gen_layout.addWidget(QLabel("Generator:"))
        self.generator_combo = QComboBox()
        generators = ["Ninja", "Ninja Multi-Config", "Unix Makefiles"]
        if get_current_platform() == Platform.WINDOWS:
            generators.extend(["Visual Studio 17 2022", "Visual Studio 16 2019", "NMake Makefiles"])
        elif get_current_platform() == Platform.MACOS:
            generators.append("Xcode")
        self.generator_combo.addItems(generators)
        gen_layout.addWidget(self.generator_combo, 1)
        settings_layout.addLayout(gen_layout)
        
        # Compiler (Windows only)
        if get_current_platform() == Platform.WINDOWS:
            compiler_layout = QHBoxLayout()
            compiler_layout.addWidget(QLabel("Compiler:"))
            self.compiler_combo = QComboBox()
            self.compiler_combo.addItems(["MSVC", "Clang", "GCC (MinGW)"])
            compiler_layout.addWidget(self.compiler_combo, 1)
            settings_layout.addLayout(compiler_layout)
        else:
            self.compiler_combo = None
        
        layout.addWidget(settings_group)
        
        # Build Configurations group
        configs_group = QGroupBox("Build Configurations")
        configs_layout = QVBoxLayout(configs_group)
        
        # Configurations list with checkboxes
        self.configs_list = QListWidget()
        self.configs_list.setMinimumHeight(150)
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
        
        # Build type
        idx = self.build_type_combo.findText(self._config.build_type)
        if idx >= 0:
            self.build_type_combo.setCurrentIndex(idx)
        
        # Generator
        idx = self.generator_combo.findText(self._config.generator)
        if idx >= 0:
            self.generator_combo.setCurrentIndex(idx)
        
        # Compiler (Windows only)
        if self.compiler_combo:
            idx = self.compiler_combo.findText(self._config.compiler)
            if idx >= 0:
                self.compiler_combo.setCurrentIndex(idx)
        
        # Build configurations
        self._load_configurations()
    
    def _load_configurations(self):
        """Load build configurations into the list."""
        self.configs_list.clear()
        
        for config in self._config.build_configurations:
            item = QListWidgetItem()
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked if config.enabled else Qt.CheckState.Unchecked)
            
            # Display format: "Name (Compiler/Generator) -> build/Dir"
            build_dir = get_build_dir_name(config.compiler)
            item.setText(f"{config.name} ({config.compiler}/{config.generator}) -> build/{build_dir}")
            item.setData(Qt.ItemDataRole.UserRole, config)
            
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
        del configs[current_row]
        self._config.build_configurations = configs
        self._load_configurations()
    
    def _save_and_accept(self):
        """Save settings and close dialog."""
        # Save build settings
        self._config.build_type = self.build_type_combo.currentText()
        self._config.generator = self.generator_combo.currentText()
        if self.compiler_combo:
            self._config.compiler = self.compiler_combo.currentText()
        
        # Save configuration enabled states
        for i in range(self.configs_list.count()):
            item = self.configs_list.item(i)
            config: BuildConfiguration = item.data(Qt.ItemDataRole.UserRole)
            config.enabled = item.checkState() == Qt.CheckState.Checked
        
        configs = []
        for i in range(self.configs_list.count()):
            item = self.configs_list.item(i)
            configs.append(item.data(Qt.ItemDataRole.UserRole))
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
        """Create pipeline builder group."""
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
        
        # Pipeline steps
        layout.addWidget(QLabel("Steps:"))
        
        self.steps_list = QListWidget()
        self.steps_list.setDragDropMode(QListWidget.DragDropMode.InternalMove)
        layout.addWidget(self.steps_list)
        
        # Step controls
        step_buttons = QHBoxLayout()
        
        self.action_combo = QComboBox()
        for action_id, action in get_available_actions().items():
            self.action_combo.addItem(action.name, action_id)
        step_buttons.addWidget(self.action_combo, 1)
        
        add_btn = QPushButton("+")
        add_btn.setMaximumWidth(30)
        add_btn.clicked.connect(self._add_step)
        step_buttons.addWidget(add_btn)
        
        remove_btn = QPushButton("-")
        remove_btn.setMaximumWidth(30)
        remove_btn.clicked.connect(self._remove_step)
        step_buttons.addWidget(remove_btn)
        
        layout.addLayout(step_buttons)
        
        return group
    
    
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
    
    def _load_pipeline_preset(self, preset_name: str):
        """Load a pipeline preset into the steps list."""
        self.steps_list.clear()
        
        pipelines = self.config.pipelines
        if preset_name in pipelines:
            for action_id in pipelines[preset_name]:
                if action_id in ACTIONS:
                    item = QListWidgetItem(ACTIONS[action_id].name)
                    item.setData(Qt.ItemDataRole.UserRole, action_id)
                    self.steps_list.addItem(item)
                elif action_id == "open_ide":
                    item = QListWidgetItem("Open IDE")
                    item.setData(Qt.ItemDataRole.UserRole, "open_ide")
                    self.steps_list.addItem(item)
        
        self.config.last_pipeline = preset_name
    
    def _add_step(self):
        """Add a step to the pipeline."""
        action_id = self.action_combo.currentData()
        action_name = self.action_combo.currentText()
        
        item = QListWidgetItem(action_name)
        item.setData(Qt.ItemDataRole.UserRole, action_id)
        self.steps_list.addItem(item)
    
    def _remove_step(self):
        """Remove selected step from the pipeline."""
        current_row = self.steps_list.currentRow()
        if current_row >= 0:
            self.steps_list.takeItem(current_row)
    
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
        """Get current pipeline from UI."""
        steps = []
        for i in range(self.steps_list.count()):
            item = self.steps_list.item(i)
            action_id = item.data(Qt.ItemDataRole.UserRole)
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
        
        self.worker = PipelineWorker(
            self.executor,
            pipeline,
            self.config.build_type,
            self.config.generator,
            self.config.compiler,
            self.config.last_ide
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
