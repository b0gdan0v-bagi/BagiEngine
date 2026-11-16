"""What the tests of tools/schema share: the paths, the import roots of the tools, and the xmllint skip mark."""

import shutil
import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent
FIXTURES = TESTS_DIR / "fixtures"
CONTROL_DIR = TOOL_DIR.parent / "control"

for import_root in (TOOL_DIR, CONTROL_DIR, CONTROL_DIR / "tests"):
    if str(import_root) not in sys.path:
        sys.path.insert(0, str(import_root))

needs_xmllint = pytest.mark.skipif(shutil.which("xmllint") is None, reason="xmllint (libxml2) is not on PATH: the XSD is not checked against XML here")
