"""Main window for the BagiEngine Launcher."""

from pathlib import Path
from typing import Optional

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGroupBox, QPushButton, QComboBox, QLabel,
    QTextEdit, QListWidget, QListWidgetItem, QProgressBar,
    QSplitter, QFrame, QMessageBox, QCheckBox
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QFont

from .config import Config
from .actions import ACTIONS, get_available_actions, ActionResult, get_current_platform, Platform
from .pipeline import Pipeline, PipelineExecutor, PipelineResult


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


class MainWindow(QMainWindow):
    """Main application window."""
    
    def __init__(self, project_root: Path):
        super().__init__()
        self.project_root = project_root
        self.config = Config(project_root / "launcher_config.json")
        self.executor = PipelineExecutor(project_root)
        self.worker: Optional[PipelineWorker] = None
        
        self.setWindowTitle("BagiEngine Launcher")
        self.setMinimumSize(800, 600)
        
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
        
        # Settings group
        settings_group = self._create_settings_group()
        left_layout.addWidget(settings_group)
        
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
        
        # Open IDE button - uses selected IDE
        btn = QPushButton("Open IDE")
        btn.clicked.connect(self._open_selected_ide)
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
    
    def _create_settings_group(self) -> QGroupBox:
        """Create settings group."""
        group = QGroupBox("Settings")
        layout = QVBoxLayout(group)
        
        # Build type
        build_layout = QHBoxLayout()
        build_layout.addWidget(QLabel("Build Type:"))
        self.build_type_combo = QComboBox()
        self.build_type_combo.addItems(["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])
        self.build_type_combo.currentTextChanged.connect(self._on_build_type_changed)
        build_layout.addWidget(self.build_type_combo, 1)
        layout.addLayout(build_layout)
        
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
        self.generator_combo.currentTextChanged.connect(self._on_generator_changed)
        gen_layout.addWidget(self.generator_combo, 1)
        layout.addLayout(gen_layout)
        
        # Compiler (Windows only - choice between MSVC, Clang, GCC)
        if get_current_platform() == Platform.WINDOWS:
            compiler_layout = QHBoxLayout()
            compiler_layout.addWidget(QLabel("Compiler:"))
            self.compiler_combo = QComboBox()
            self.compiler_combo.addItems(["MSVC", "Clang", "GCC (MinGW)"])
            self.compiler_combo.currentTextChanged.connect(self._on_compiler_changed)
            compiler_layout.addWidget(self.compiler_combo, 1)
            layout.addLayout(compiler_layout)
        else:
            self.compiler_combo = None
        
        # IDE selector
        ide_layout = QHBoxLayout()
        ide_layout.addWidget(QLabel("IDE:"))
        self.ide_combo = QComboBox()
        self.ide_combo.addItem("Cursor", "cursor")
        if get_current_platform() == Platform.WINDOWS:
            self.ide_combo.addItem("Visual Studio", "vs")
        elif get_current_platform() == Platform.MACOS:
            self.ide_combo.addItem("Xcode", "xcode")
        self.ide_combo.currentIndexChanged.connect(self._on_ide_changed)
        ide_layout.addWidget(self.ide_combo, 1)
        layout.addLayout(ide_layout)
        
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
        # Build type
        idx = self.build_type_combo.findText(self.config.build_type)
        if idx >= 0:
            self.build_type_combo.setCurrentIndex(idx)
        
        # Generator
        idx = self.generator_combo.findText(self.config.generator)
        if idx >= 0:
            self.generator_combo.setCurrentIndex(idx)
        
        # Compiler (Windows only)
        if self.compiler_combo:
            idx = self.compiler_combo.findText(self.config.compiler)
            if idx >= 0:
                self.compiler_combo.setCurrentIndex(idx)
        
        # IDE
        idx = self.ide_combo.findData(self.config.last_ide)
        if idx >= 0:
            self.ide_combo.setCurrentIndex(idx)
        
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
    
    def _open_selected_ide(self):
        """Open the currently selected IDE."""
        ide = self.ide_combo.currentData()
        action_id = f"open_{ide}" if ide != "cursor" else "open_cursor"
        
        # For "open_ide" action, use the generic handler
        pipeline = Pipeline("open_ide", ["open_ide"])
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
        
        # Get compiler (only on Windows, default to MSVC otherwise)
        compiler = self.compiler_combo.currentText() if self.compiler_combo else "MSVC"
        
        self.worker = PipelineWorker(
            self.executor,
            pipeline,
            self.build_type_combo.currentText(),
            self.generator_combo.currentText(),
            compiler,
            self.ide_combo.currentData()
        )
        
        self.worker.step_started.connect(self._on_step_started)
        self.worker.step_completed.connect(self._on_step_completed)
        self.worker.output_received.connect(self._log)
        self.worker.finished_signal.connect(self._on_pipeline_finished)
        
        self.worker.start()
    
    def _cancel_pipeline(self):
        """Cancel the running pipeline."""
        if self.worker:
            self.executor.cancel()
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
    
    def _on_build_type_changed(self, value: str):
        """Handle build type change."""
        self.config.build_type = value
    
    def _on_generator_changed(self, value: str):
        """Handle generator change."""
        self.config.generator = value
    
    def _on_compiler_changed(self, value: str):
        """Handle compiler change."""
        self.config.compiler = value
    
    def _on_ide_changed(self, index: int):
        """Handle IDE change."""
        self.config.last_ide = self.ide_combo.currentData()
    
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
            self.worker.wait()
        event.accept()
