import sys
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
CONTROL_DIR = TOOL_DIR.parent / "control"

sys.path.insert(0, str(CONTROL_DIR))
sys.path.insert(0, str(TOOL_DIR))
