import importlib.util
import sys
import textwrap
from pathlib import Path
from typing import List, Tuple

import pytest

TOOL_DIR = Path(__file__).resolve().parents[1]

sys.path.insert(0, str(TOOL_DIR))

from core.models import ClassData, ParseWarning  # noqa: E402
from core.parser import create_parser  # noqa: E402


@pytest.fixture
def parse(tmp_path):
    """Write a header into a temp directory and parse it: returns (classes, warnings)."""
    counter = [0]

    def run(source: str) -> Tuple[List[ClassData], List[ParseWarning]]:
        counter[0] += 1
        path = tmp_path / f"Header{counter[0]}.h"
        path.write_text(textwrap.dedent(source).lstrip("\n"), encoding="utf-8")
        parser = create_parser([], set())
        classes, _ = parser.parse_file(path)
        return classes, parser.last_warnings

    return run


@pytest.fixture
def run_cli(monkeypatch, capsys, tmp_path):
    """Run `meta_generator.main()` on a source directory: returns (exit code, stdout, stderr).

    The output and the cache directories live in tmp_path, so two calls with the same
    source directory share a cache (the second one is an incremental run).
    """
    pytest.importorskip("jinja2")
    # `meta_generator` alone names the package directory when pytest puts CI/ on sys.path
    spec = importlib.util.spec_from_file_location("meta_generator_cli", TOOL_DIR / "meta_generator.py")
    meta_generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(meta_generator)

    def run(source_dir, *extra: str):
        argv = [
            "meta_generator.py",
            "--source-dir", str(source_dir),
            "--output-dir", str(tmp_path / "out"),
            "--cache-dir", str(tmp_path / "cache"),
            *extra,
        ]
        monkeypatch.setattr("sys.argv", argv)
        code = meta_generator.main()
        captured = capsys.readouterr()
        return code, captured.out, captured.err

    return run
