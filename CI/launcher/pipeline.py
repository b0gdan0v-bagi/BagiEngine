"""Pipeline execution logic."""

from dataclasses import dataclass, field
from typing import Callable, Optional, TYPE_CHECKING
from pathlib import Path

from .actions import ActionExecutor, ActionResult, ACTIONS, get_build_dir_name

if TYPE_CHECKING:
    from .config import BuildConfiguration


@dataclass
class PipelineStep:
    """A single step in a pipeline."""
    action_id: str
    params: dict
    
    @property
    def name(self) -> str:
        if self.action_id in ACTIONS:
            return ACTIONS[self.action_id].name
        return self.action_id


@dataclass
class PipelineResult:
    """Result of pipeline execution."""
    success: bool
    steps_completed: int
    total_steps: int
    results: list[tuple[str, ActionResult]]
    
    @property
    def summary(self) -> str:
        if self.success:
            return f"Pipeline completed: {self.steps_completed}/{self.total_steps} steps"
        else:
            return f"Pipeline failed at step {self.steps_completed + 1}/{self.total_steps}"


class Pipeline:
    """Manages and executes a sequence of actions."""
    
    def __init__(self, name: str, steps: list[str]):
        self.name = name
        self._steps = [PipelineStep(action_id=s, params={}) for s in steps]
    
    @property
    def steps(self) -> list[PipelineStep]:
        return self._steps
    
    def add_step(self, action_id: str, params: Optional[dict] = None) -> None:
        """Add a step to the pipeline."""
        self._steps.append(PipelineStep(action_id=action_id, params=params or {}))
    
    def remove_step(self, index: int) -> None:
        """Remove a step from the pipeline."""
        if 0 <= index < len(self._steps):
            self._steps.pop(index)
    
    def clear(self) -> None:
        """Clear all steps."""
        self._steps.clear()


class PipelineExecutor:
    """Executes pipelines with progress callbacks."""
    
    def __init__(self, project_root: Path):
        self.action_executor = ActionExecutor(project_root)
        self._cancelled = False
    
    def execute(
        self,
        pipeline: Pipeline,
        build_type: str = "Debug",
        generator: str = "Ninja",
        compiler: str = "MSVC",
        ide: str = "cursor",
        vs_build_dirs: Optional[list[str]] = None,
        on_step_start: Optional[Callable[[int, str], None]] = None,
        on_step_complete: Optional[Callable[[int, str, ActionResult], None]] = None,
        on_output: Optional[Callable[[str], None]] = None,
    ) -> PipelineResult:
        """Execute all steps in the pipeline."""
        self._cancelled = False
        results: list[tuple[str, ActionResult]] = []
        
        for i, step in enumerate(pipeline.steps):
            if self._cancelled:
                return PipelineResult(
                    success=False,
                    steps_completed=i,
                    total_steps=len(pipeline.steps),
                    results=results
                )
            
            if on_step_start:
                on_step_start(i, step.name)
            
            # Prepare params based on action type
            params = {
                "build_type": build_type,
                "generator": generator,
                "compiler": compiler,
                "ide": ide,
                "vs_build_dirs": vs_build_dirs,
                **step.params
            }
            
            result = self.action_executor.execute(step.action_id, **params)
            results.append((step.action_id, result))
            
            if on_output and result.output:
                on_output(result.output)
            if on_output and result.error:
                on_output(f"[ERROR] {result.error}")
            
            if on_step_complete:
                on_step_complete(i, step.name, result)
            
            if not result.success:
                return PipelineResult(
                    success=False,
                    steps_completed=i,
                    total_steps=len(pipeline.steps),
                    results=results
                )
        
        return PipelineResult(
            success=True,
            steps_completed=len(pipeline.steps),
            total_steps=len(pipeline.steps),
            results=results
        )
    
    def cancel(self) -> None:
        """Cancel the current pipeline execution."""
        self._cancelled = True


@dataclass
class ConfigPipelineResult:
    """Result of pipeline execution for a single configuration."""
    config_name: str
    build_dir: str
    pipeline_result: PipelineResult
    
    @property
    def success(self) -> bool:
        return self.pipeline_result.success


@dataclass
class MultiConfigPipelineResult:
    """Result of multi-configuration pipeline execution."""
    success: bool
    configs_completed: int
    total_configs: int
    results: list[ConfigPipelineResult]
    
    @property
    def summary(self) -> str:
        successful = sum(1 for r in self.results if r.success)
        failed = len(self.results) - successful
        if self.success:
            return f"All {self.total_configs} configuration(s) completed successfully"
        else:
            return f"Completed {successful}/{self.total_configs} configurations ({failed} failed)"


class MultiConfigPipelineExecutor:
    """Executes pipelines for multiple build configurations."""
    
    def __init__(self, project_root: Path):
        self.project_root = project_root
        self._cancelled = False
    
    def execute(
        self,
        pipeline: Pipeline,
        configurations: list["BuildConfiguration"],
        ide: str = "cursor",
        on_config_start: Optional[Callable[[int, str, str], None]] = None,
        on_config_complete: Optional[Callable[[int, str, bool], None]] = None,
        on_step_start: Optional[Callable[[int, str], None]] = None,
        on_step_complete: Optional[Callable[[int, str, ActionResult], None]] = None,
        on_output: Optional[Callable[[str], None]] = None,
    ) -> MultiConfigPipelineResult:
        """Execute pipeline for all enabled configurations.
        
        Args:
            pipeline: Pipeline to execute
            configurations: List of build configurations
            ide: IDE to open (for open_ide action)
            on_config_start: Callback(config_index, config_name, build_dir)
            on_config_complete: Callback(config_index, config_name, success)
            on_step_start: Callback(step_index, step_name)
            on_step_complete: Callback(step_index, step_name, result)
            on_output: Callback(output_text)
        """
        self._cancelled = False
        results: list[ConfigPipelineResult] = []
        
        # Filter only enabled configurations
        enabled_configs = [c for c in configurations if c.enabled]
        
        if not enabled_configs:
            return MultiConfigPipelineResult(
                success=False,
                configs_completed=0,
                total_configs=0,
                results=[]
            )
        
        for i, config in enumerate(enabled_configs):
            if self._cancelled:
                return MultiConfigPipelineResult(
                    success=False,
                    configs_completed=i,
                    total_configs=len(enabled_configs),
                    results=results
                )
            
            build_dir = get_build_dir_name(config.compiler)
            
            if on_config_start:
                on_config_start(i, config.name, build_dir)
            
            if on_output:
                on_output(f"\n{'='*60}")
                on_output(f"Configuration: {config.name}")
                on_output(f"Build directory: build/{build_dir}")
                on_output(f"Compiler: {config.compiler}, Generator: {config.generator}")
                on_output(f"{'='*60}\n")
            
            # Create executor for this configuration
            executor = PipelineExecutor(self.project_root)
            
            # Execute pipeline with this configuration's settings
            pipeline_result = executor.execute(
                pipeline=pipeline,
                build_type=config.build_type,
                generator=config.generator,
                compiler=config.compiler,
                ide=ide,
                on_step_start=on_step_start,
                on_step_complete=on_step_complete,
                on_output=on_output,
            )
            
            config_result = ConfigPipelineResult(
                config_name=config.name,
                build_dir=build_dir,
                pipeline_result=pipeline_result
            )
            results.append(config_result)
            
            if on_config_complete:
                on_config_complete(i, config.name, pipeline_result.success)
            
            # Continue to next configuration even if this one failed
            # (unless cancelled)
        
        all_success = all(r.success for r in results)
        
        return MultiConfigPipelineResult(
            success=all_success,
            configs_completed=len(results),
            total_configs=len(enabled_configs),
            results=results
        )
    
    def cancel(self) -> None:
        """Cancel the current multi-config pipeline execution."""
        self._cancelled = True
