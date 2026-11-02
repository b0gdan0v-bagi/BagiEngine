import sys
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
REPO_ROOT = TOOL_DIR.parents[1]

sys.path.insert(0, str(TOOL_DIR))


@pytest.fixture
def sample_todo() -> Path:
    return FIXTURES / "todo"


@pytest.fixture
def sample_plans() -> Path:
    return FIXTURES / "plans"


@pytest.fixture
def sample_root() -> Path:
    return FIXTURES
