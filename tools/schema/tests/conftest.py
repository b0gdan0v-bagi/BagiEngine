import json

import pytest
from schema_support import FIXTURES


@pytest.fixture
def schema() -> dict:
    return json.loads((FIXTURES / "schema.json").read_text(encoding="utf-8"))
