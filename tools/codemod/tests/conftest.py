import sys
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
ENGINE_ROOT = TOOL_DIR.parent.parent

sys.path.insert(0, str(TOOL_DIR))
