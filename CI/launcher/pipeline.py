"""Pipeline execution logic."""

from dataclasses import dataclass
from typing import Callable, Optional
from pathlib import Path

from .actions import ActionExecutor, ActionResult, ACTIONS


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
