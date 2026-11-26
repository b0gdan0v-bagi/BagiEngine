"""Shared by the tests of tools/export: a game that looks built, made of text files in a temp folder."""

import json
import sys
from pathlib import Path

# tools/export, where the modules under test are (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

TARGET = "Game"


def write(path: Path, text: str = "") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text or path.name, encoding="utf-8")
    return path


class FakeGame:
    """A project whose game root and engine root hold content, and a build folder with the manifest of `Game`."""

    def __init__(self, root: Path, preset: str = "preset"):
        self.project = root / "project"
        self.engine_root = self.project / "engine"
        self.game_root = self.project / "game"
        self.build = self.project / "build" / "export" / preset
        self.executable = write(self.build / "bin" / TARGET)
        self.executable.chmod(0o755)
        self.notices = write(self.build / "bin" / "THIRD_PARTY_NOTICES.txt")
        self.library = write(self.build / "lib" / "libfake.2.0.dylib")
        for content in (self.engine_root, self.game_root):
            write(content / "config" / "App.xml", f"<{content.name}/>")
            write(content / "assets" / "art" / f"{content.name}.png")
        self.manifest = write(self.build / "export" / f"{TARGET}.json", json.dumps({
            "target": TARGET,
            "executable": str(self.executable),
            "engineRoot": str(self.engine_root),
            "gameRoot": str(self.game_root),
            "notices": str(self.notices),
            "libraries": [{"source": str(self.library), "name": "libfake.2.dylib"}],
        }))


def tree(folder: Path) -> dict[str, bytes | None]:
    """Every file (its bytes) and folder (None) below `folder`: equal trees mean nothing was written or removed."""
    return {path.relative_to(folder).as_posix(): path.read_bytes() if path.is_file() else None for path in sorted(folder.rglob("*"))}
