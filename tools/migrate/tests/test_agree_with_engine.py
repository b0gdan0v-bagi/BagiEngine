"""The engine's content version exists twice: `kContentVersion` in C++ and the highest `to_version` of the engine track here.
The two languages cannot share the number, so this test fails when only one of them was bumped."""

import re
import sys

import pytest

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


UI_SOURCES = ENGINE_ROOT / "src" / "Modules" / "BECore" / "UI"


def cpp_constant(header: str, name: str) -> float:
    match = re.search(rf"\b{name}\s*(?:=|\{{)\s*(-?[\d.]+)f?\s*[;}}]", (UI_SOURCES / header).read_text(encoding="utf-8"))
    assert match, f"no `{name} = N;` in {header}"
    return float(match.group(1))


def value_module():
    import importlib.util

    from conftest import TOOL_DIR

    sys.path.insert(0, str(TOOL_DIR / "steps"))
    spec = importlib.util.spec_from_file_location("ui_value_under_test", TOOL_DIR / "steps" / "_ui_value.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_toggle_step_writes_the_numbers_of_the_headers():
    value = value_module()
    assert float(value.SWITCH_AREA_ASPECT) == cpp_constant("UISwitchComponent.h", "kAreaAspect") == 1.25
    assert float(value.CAPTION_GAP) == cpp_constant("UISwitchComponent.h", "kCaptionGap")
    assert value.OFF_TRACK_ALPHA == pytest.approx(cpp_constant("UISwitchComponent.h", "kOffTrackAlpha"))
    # the track is kSwitchHeightRatio x kSwitchAspect of the height, plus the air of a quarter of the height at the right end: the area is that much wider than high
    ratio = cpp_constant("UISwitchComponent.h", "kSwitchHeightRatio")
    aspect = cpp_constant("UISwitchComponent.h", "kSwitchAspect")
    assert ratio * aspect + (1 - ratio) / 2 == pytest.approx(float(value.SWITCH_AREA_ASPECT))
    # The numbers `UIToggle` had when it was removed (the component must keep drawing the switch the class did)
    for name, was in (("kSwitchHeightRatio", 0.5), ("kSwitchAspect", 2.0), ("kThumbInset", 3.0)):
        assert cpp_constant("UISwitchComponent.h", name) == was


def test_the_alpha_of_the_derived_track_is_cut_like_with_alpha_of_the_cpp_source():
    source = (UI_SOURCES / "UIColor.h").read_text(encoding="utf-8")
    assert "static_cast<unsigned char>(static_cast<float>(color.a) * factor)" in source  # single precision, truncated: what `with_alpha` mirrors
    value = value_module()
    assert [value.with_alpha((0, 0, 0, a), value.OFF_TRACK_ALPHA)[3] for a in (255, 128, 100, 1, 0)] == [89, 44, 35, 0, 0]

