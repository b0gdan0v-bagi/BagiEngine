"""Configuration management for the launcher."""

import json
from pathlib import Path
from typing import Any
from dataclasses import dataclass, field, asdict


@dataclass
class BuildConfiguration:
    """Represents a single build configuration."""
    name: str
    compiler: str      # MSVC, Clang, GCC
    generator: str     # Ninja, Visual Studio, etc.
    build_type: str    # Debug, Release
    enabled: bool = True
    
    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "BuildConfiguration":
        """Create from dictionary."""
        return cls(
            name=data.get("name", ""),
            compiler=data.get("compiler", "MSVC"),
            generator=data.get("generator", "Ninja"),
            build_type=data.get("build_type", "Debug"),
            enabled=data.get("enabled", True)
        )


class Config:
    """Manages launcher configuration stored in JSON."""
    
    DEFAULT_CONFIG = {
        "build_type": "Debug",
        "generator": "Ninja",
        "compiler": "MSVC",
        "pipelines": {
            "full_build": ["cmake_configure", "cmake_build", "open_vscode"],
            "configure_only": ["cmake_configure"],
            "build_only": ["cmake_build"],
            "quick_open": ["open_vscode"]
        },
        "last_pipeline": "full_build",
        "build_configurations": [
            {"name": "MSVC Debug", "compiler": "MSVC", "generator": "Ninja", "build_type": "Debug", "enabled": True},
            {"name": "Clang Debug", "compiler": "Clang", "generator": "Ninja", "build_type": "Debug", "enabled": False}
        ],
        "pipeline_step_states": {}  # Checkbox states for pipeline builder
    }

    # Action ids that were renamed; saved configs from older versions still use them
    LEGACY_ACTION_IDS = {"open_cursor": "open_vscode"}

    def __init__(self, config_path: Path):
        self.config_path = config_path
        self._data: dict[str, Any] = {}
        self.load()
    
    def load(self) -> None:
        """Load configuration from file or create default."""
        if self.config_path.exists():
            try:
                with open(self.config_path, 'r', encoding='utf-8') as f:
                    self._data = json.load(f)
            except (json.JSONDecodeError, IOError):
                self._data = self.DEFAULT_CONFIG.copy()
        else:
            self._data = self.DEFAULT_CONFIG.copy()

        self._migrate_legacy_ids()

    def _migrate_legacy_ids(self) -> None:
        """Rename action/IDE ids saved by older versions. Saves only if something changed."""
        changed = False

        for steps in self._data.get("pipelines", {}).values():
            for i, step in enumerate(steps):
                if step in self.LEGACY_ACTION_IDS:
                    steps[i] = self.LEGACY_ACTION_IDS[step]
                    changed = True

        states = self._data.get("pipeline_step_states", {})
        for old_id, new_id in self.LEGACY_ACTION_IDS.items():
            if old_id in states:
                states[new_id] = states.pop(old_id)
                changed = True

        if self._data.get("last_ide") == "cursor":
            self._data["last_ide"] = "vscode"
            changed = True

        if changed:
            self.save()

    def save(self) -> None:
        """Save configuration to file."""
        with open(self.config_path, 'w', encoding='utf-8') as f:
            json.dump(self._data, f, indent=2, ensure_ascii=False)
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value."""
        return self._data.get(key, default)
    
    def set(self, key: str, value: Any) -> None:
        """Set configuration value and save."""
        self._data[key] = value
        self.save()
    
    @property
    def last_ide(self) -> str:
        return self.get("last_ide", "vscode")
    
    @last_ide.setter
    def last_ide(self, value: str) -> None:
        self.set("last_ide", value)
    
    @property
    def build_type(self) -> str:
        return self.get("build_type", "Debug")
    
    @build_type.setter
    def build_type(self, value: str) -> None:
        self.set("build_type", value)
    
    @property
    def generator(self) -> str:
        return self.get("generator", "Ninja")
    
    @generator.setter
    def generator(self, value: str) -> None:
        self.set("generator", value)
    
    @property
    def compiler(self) -> str:
        return self.get("compiler", "MSVC")
    
    @compiler.setter
    def compiler(self, value: str) -> None:
        self.set("compiler", value)
    
    @property
    def pipelines(self) -> dict[str, list[str]]:
        return self.get("pipelines", self.DEFAULT_CONFIG["pipelines"])
    
    @property
    def last_pipeline(self) -> str:
        return self.get("last_pipeline", "full_build")
    
    @last_pipeline.setter
    def last_pipeline(self, value: str) -> None:
        self.set("last_pipeline", value)
    
    @property
    def build_configurations(self) -> list[BuildConfiguration]:
        """Get list of build configurations."""
        configs_data = self.get("build_configurations", self.DEFAULT_CONFIG["build_configurations"])
        return [BuildConfiguration.from_dict(c) for c in configs_data]
    
    @build_configurations.setter
    def build_configurations(self, value: list[BuildConfiguration]) -> None:
        """Set build configurations."""
        self.set("build_configurations", [c.to_dict() for c in value])
    
    @property
    def pipeline_step_states(self) -> dict[str, bool]:
        """Get pipeline step checkbox states."""
        return self.get("pipeline_step_states", {})
    
    @pipeline_step_states.setter
    def pipeline_step_states(self, value: dict[str, bool]) -> None:
        """Set pipeline step checkbox states."""
        self.set("pipeline_step_states", value)
