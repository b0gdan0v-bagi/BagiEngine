"""dev.py on a small repository in a temp folder: presets with an include, a dev.json, the flags in CMakeUserPresets.json."""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import dev  # noqa: E402

ENGINE_PRESETS = {
    "version": 6,
    "configurePresets": [
        {"name": "base", "hidden": True, "binaryDir": "${sourceDir}/build/${presetName}"},
        {"name": "host-macos", "hidden": True, "condition": {"type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin"}},
        {"name": "host-linux", "hidden": True, "condition": {"type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux"}},
        {"name": "macos-debug", "inherits": ["base", "host-macos"]},
        {"name": "macos-release", "inherits": ["base", "host-macos"]},
        {"name": "linux-server-release", "inherits": ["base", "host-linux"]},
        {"name": "web-debug", "inherits": ["base"]},
        {"name": "web-release", "inherits": ["base"]},
    ],
    "buildPresets": [{"name": "macos-debug", "configurePreset": "macos-debug"}, {"name": "web-release", "configurePreset": "web-release"}],
    "testPresets": [{"name": "macos-debug", "configurePreset": "macos-debug"}],
}

MANIFEST = {
    "products": [
        {"name": "game", "target": "Game", "run": ["{bin}/Game{exe}"]},
        {"name": "arena", "target": "Arena", "prefer": "release", "run": ["{bin}/Arena"]},
        {"name": "web", "kind": "web", "prefer": "release", "target": "Game", "after": [["{python}", "pack.py", "{bin}", "dist"]], "run": ["serve"]},
    ],
    "chains": [{"name": "pair", "steps": ["game", {"product": "game", "args": ["--two"]}]}],
    "tools": [{"name": "shots", "build": "game", "command": ["shots", "--exe", "{bin}/Game"]}],
}


@pytest.fixture
def repo(tmp_path: Path, monkeypatch) -> Path:
    (tmp_path / "engine").mkdir()
    (tmp_path / "engine" / "CMakePresets.json").write_text(json.dumps(ENGINE_PRESETS))
    (tmp_path / "CMakePresets.json").write_text(json.dumps({"version": 6, "include": ["engine/CMakePresets.json"]}))
    (tmp_path / "dev.json").write_text(json.dumps(MANIFEST))
    monkeypatch.setattr(dev.platform, "system", lambda: "Darwin")
    monkeypatch.chdir(tmp_path)
    return tmp_path


def test_presets_are_grouped_for_this_host_through_the_include(repo: Path):
    groups = dev.preset_groups(repo)
    assert groups["desktop"] == ["macos-debug", "macos-release"]
    assert groups["web"] == ["web-debug", "web-release"]
    assert "linux-server-release" not in sum(groups.values(), [])  # its host condition fails on Darwin
    assert dev.default_preset(repo, "desktop") == "macos-debug"
    assert dev.default_preset(repo, "desktop", "release") == "macos-release"
    assert dev.default_preset(repo, "web", "release") == "web-release"


def test_flags_make_three_dev_presets_and_go_away_with_the_last_flag(repo: Path):
    user = repo / "CMakeUserPresets.json"
    user.write_text(json.dumps({"version": 6, "configurePresets": [{"name": "mine", "inherits": "macos-debug"}]}))
    result = dev.set_flags(repo, "macos-debug", {"ENGINE_PROFILER": "ON", "ENGINE_PROFILER_ALLOC": "ON"}, [])
    assert result == {"preset": "macos-debug-dev", "base": "macos-debug", "flags": {"ENGINE_PROFILER": "ON", "ENGINE_PROFILER_ALLOC": "ON"}}
    data = json.loads(user.read_text())
    names = {kind: [p["name"] for p in data.get(kind, [])] for kind in ("configurePresets", "buildPresets", "testPresets")}
    assert names == {"configurePresets": ["mine", "macos-debug-dev"], "buildPresets": ["macos-debug-dev"], "testPresets": ["macos-debug-dev"]}
    assert data["buildPresets"][0] == {"name": "macos-debug-dev", "inherits": "macos-debug", "configurePreset": "macos-debug-dev"}
    assert dev.preset_groups(repo)["user"] == ["mine", "macos-debug-dev"]
    assert dev.kind_of_preset("macos-debug-dev") == "desktop"

    dev.set_flags(repo, "macos-debug-dev", {}, ["ENGINE_PROFILER_ALLOC"])
    assert dev.dev_presets(repo) == {"macos-debug-dev": {"base": "macos-debug", "flags": {"ENGINE_PROFILER": "ON"}}}
    back = dev.set_flags(repo, "macos-debug", {}, ["ENGINE_PROFILER"])
    assert back["preset"] == "macos-debug" and dev.dev_presets(repo) == {}
    assert json.loads(user.read_text())["configurePresets"] == [{"name": "mine", "inherits": "macos-debug"}]  # the owner's preset stays


def test_a_web_base_without_a_test_preset_gets_none_and_the_last_flag_removes_the_file(repo: Path):
    dev.set_flags(repo, "web-release", {"ENGINE_SINGLE_THREAD": "ON"}, [])
    data = json.loads((repo / "CMakeUserPresets.json").read_text())
    assert "testPresets" not in data and data["buildPresets"][0]["inherits"] == "web-release"
    dev.set_flags(repo, "web-release", {}, ["ENGINE_SINGLE_THREAD"])
    assert not (repo / "CMakeUserPresets.json").exists()
    with pytest.raises(dev.DevError):
        dev.set_flags(repo, "nope", {"A": "1"}, [])


def test_build_configures_only_without_a_cache_then_builds_and_runs_the_after_steps(repo: Path):
    web = dev.product_of(dev.load_manifest(repo), "web")
    commands = dev.build_commands(repo, web, "web-release", False)
    assert commands[0] == ["cmake", "--preset", "web-release"]
    assert commands[1] == ["cmake", "--build", "--preset", "web-release", "--target", "Game"]
    assert commands[2] == [sys.executable, "pack.py", str(repo / "build" / "web-release" / "bin"), "dist"]
    (repo / "build" / "web-release").mkdir(parents=True)
    (repo / "build" / "web-release" / "CMakeCache.txt").write_text("")
    assert dev.build_commands(repo, web, "web-release", False)[0][:2] == ["cmake", "--build"]
    assert dev.build_commands(repo, web, "web-release", True)[0] == ["cmake", "--preset", "web-release"]


def test_a_product_takes_its_preferred_preset_and_refuses_one_of_the_other_kind(repo: Path):
    manifest = dev.load_manifest(repo)
    assert dev.pick_preset(repo, dev.product_of(manifest, "arena"), None) == "macos-release"
    assert dev.pick_preset(repo, dev.product_of(manifest, "web"), None) == "web-release"
    with pytest.raises(dev.DevError):
        dev.pick_preset(repo, dev.product_of(manifest, "game"), "web-debug")
    with pytest.raises(dev.DevError):
        dev.product_of(manifest, "missing")


def test_info_json_names_everything_the_panel_shows(repo: Path, capsys):
    assert dev.main(["info", "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["manifest"] and info["defaults"] == {"desktop": "macos-debug", "web": "web-debug"}
    assert [p["name"] for p in info["products"]] == ["game", "arena", "web"]
    assert info["flags"] == dev.DEFAULT_FLAGS  # the manifest names none
    assert dev.main(["flags", "macos-debug", "--set", "BAD"]) == 1


def test_the_venv_of_the_engine_comes_first_on_the_path(repo: Path):
    (repo / "engine" / ".venv" / "bin").mkdir(parents=True)
    env = dev.tool_env(repo, {"X": 1})
    assert env["PATH"].split(dev.os.pathsep)[0] == str(repo / "engine" / ".venv" / "bin") and env["X"] == "1"


def test_a_tool_builds_its_product_first_and_gets_its_bin(repo: Path, monkeypatch):
    calls = []
    monkeypatch.setattr(dev, "run_step", lambda command, root, env=None: calls.append(command))
    monkeypatch.setattr(dev.subprocess, "run", lambda command, **kw: calls.append(command) or type("R", (), {"returncode": 0})())
    with pytest.raises(SystemExit):
        dev.main(["tool", "shots", "--", "--seed", "3"])
    assert calls[0] == ["cmake", "--preset", "macos-debug"]
    assert calls[-1] == ["shots", "--exe", str(repo / "build" / "macos-debug" / "bin" / "Game"), "--seed", "3"]
