"""Configuration management for the launcher."""

import json
from pathlib import Path
from typing import Any


class Config:
    """Manages launcher configuration stored in JSON."""
    
    DEFAULT_CONFIG = {
        "last_ide": "cursor",
        "build_type": "Debug",
        "generator": "Ninja",
        "compiler": "MSVC",
        "pipelines": {
            "full_build": ["cmake_configure", "cmake_build", "open_ide"],
            "configure_only": ["cmake_configure"],
            "build_only": ["cmake_build"],
            "quick_open": ["open_ide"]
        },
        "last_pipeline": "full_build"
    }
    
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
        return self.get("last_ide", "cursor")
    
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
