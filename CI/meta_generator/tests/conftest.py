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
