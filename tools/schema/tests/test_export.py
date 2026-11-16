"""export_schema against a fake game that serves `schema` (fake_schema_game.py) and against the fake control server alone."""

import json
import sys
from pathlib import Path

import pytest
from schema_support import FIXTURES, TESTS_DIR

import export_schema
from bagi_control_client import BagiControlClient
from fake_control_server import FakeControlServer

FAKE_GAME = TESTS_DIR / "fake_schema_game.py"
pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="the fake game is a script with a shebang here")


@pytest.fixture
def fake_game_exe(tmp_path, monkeypatch):
    exe = tmp_path / "FakeGame"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_GAME)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    monkeypatch.setenv("FAKE_SCHEMA_FILE", str(FIXTURES / "schema.json"))
    return exe


def run(monkeypatch, *arguments: str) -> int:
    monkeypatch.setattr("sys.argv", ["export_schema.py", *arguments])
    return export_schema.main()


def test_fetch_asks_for_the_schema_method_alone_and_checks_the_version():
    schema = json.loads((FIXTURES / "schema.json").read_text(encoding="utf-8"))
    server = FakeControlServer(lambda method, params: schema)
    port = server.start()
    try:
        with BagiControlClient(port=port) as client:
            assert export_schema.fetch_schema(client) == schema
        assert server.requests == [("schema", None)]
    finally:
        server.stop()


def test_a_schema_of_another_version_is_refused():
    server = FakeControlServer(lambda method, params: {"version": 2, "types": []})
    port = server.start()
    try:
        with BagiControlClient(port=port) as client, pytest.raises(export_schema.SchemaVersionError, match="version 2"):
            export_schema.fetch_schema(client)
    finally:
        server.stop()


def test_the_game_is_started_asked_and_quit_and_the_json_is_written(fake_game_exe, tmp_path, monkeypatch, capsys):
    out = tmp_path / "schema.json"
    assert run(monkeypatch, str(fake_game_exe), "--out", str(out)) == 0
    assert json.loads(out.read_text(encoding="utf-8")) == json.loads((FIXTURES / "schema.json").read_text(encoding="utf-8"))
    assert "wrote" in capsys.readouterr().out


def test_without_out_the_json_goes_to_stdout(fake_game_exe, monkeypatch, capsys):
    assert run(monkeypatch, str(fake_game_exe)) == 0
    assert json.loads(capsys.readouterr().out)["version"] == 1


def test_a_game_that_does_not_know_the_method_fails_with_its_code(fake_game_exe, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_SCHEMA_REFUSE", "1")
    assert run(monkeypatch, str(fake_game_exe)) == 1
    assert "unknown_method" in capsys.readouterr().err


def test_another_version_is_a_usage_error(fake_game_exe, monkeypatch, capsys):
    monkeypatch.setenv("FAKE_SCHEMA_VERSION", "2")
    assert run(monkeypatch, str(fake_game_exe)) == 2
    assert "version 2" in capsys.readouterr().err


def test_a_missing_executable_is_skipped_not_failed(tmp_path, monkeypatch, capsys):
    assert run(monkeypatch, str(tmp_path / "NoGame")) == 77
    assert "SKIPPED" in capsys.readouterr().out
    assert run(monkeypatch, "--preset", "p", "--game", "G", "--root", str(tmp_path)) == 77


@pytest.mark.parametrize("arguments", [[], ["x", "--preset", "p", "--game", "G"], ["--preset", "p"], ["--game", "G"]])
def test_the_game_is_named_by_a_path_or_by_preset_and_game(monkeypatch, arguments):
    with pytest.raises(SystemExit) as stop:
        run(monkeypatch, *arguments)
    assert stop.value.code == 2
