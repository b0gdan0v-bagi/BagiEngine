"""The engine's content version exists twice: `kContentVersion` in C++ and the highest `to_version` of the engine track here.
The two languages cannot share the number, so this test fails when only one of them was bumped."""

import re

import migrate
from conftest import ENGINE_ROOT

HEADER = ENGINE_ROOT / "src" / "Modules" / "BECore" / "Config" / "ContentVersion.h"


def cpp_content_version() -> int:
    match = re.search(r"kContentVersion\s*=\s*(\d+)\s*;", HEADER.read_text(encoding="utf-8"))
    assert match, f"no `kContentVersion = N;` in {HEADER}"
    return int(match.group(1))


def test_the_cpp_version_is_the_highest_step_of_the_engine_track():
    registry = migrate.build_registry()
    assert cpp_content_version() == registry.latest(migrate.ENGINE_TRACK), (
        "kContentVersion (ContentVersion.h) and the highest to_version of tools/migrate/steps/ differ: "
        "a format change adds a step AND bumps kContentVersion"
    )


def test_the_attribute_names_agree():
    text = HEADER.read_text(encoding="utf-8")
    assert f'kContentVersionAttribute = "{migrate.VERSION_ATTRIBUTE}"' in text
