"""The session behind the MCP tools (bagi_session.py) against a fake control API, and the MCP server over stdio."""

import asyncio
import base64
import os
import sys
import time
from pathlib import Path

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_session import GameSession, SessionError
from fake_control_server import FakeControlServer, FakeError, QuitServer
from game_process import free_port

TESTS_DIR = Path(__file__).resolve().parent
TOOL_DIR = TESTS_DIR.parent


def echo_handler(method: str, params: dict | None):
    """Answers like a small game; a request is echoed so that a test can see what arrived."""
    if method == "status":
        return {"frame": 7, "manualClock": True}
    if method == "list_games":
        return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
    if method == "get_state":
        return {"game": "Fake", "version": 3, "state": {"board": "x"}}
    if method == "perform_action":
        if (params or {}).get("type") == "bad":
            raise FakeError("illegal_action", "not allowed")
        return {"game": "Fake", "version": 4, "outcome": params}
    if method == "step":
        return {"frame": 8, "params": params}
    if method == "screenshot":
        return {"width": 2, "height": 1, "png_base64": base64.b64encode(b"\x89PNG-fake").decode("ascii"), "params": params}
    if method == "ui_tree":
        return {"scene": "Fake", "canvases": []}
    if method == "ui_click":
        if (params or {}).get("name") == "Nothing":
            raise FakeError("not_found", "no element named 'Nothing'")
        return {"clicked": (params or {}).get("name"), "params": params}
    if method == "frame_stats":
        return {"frames": [{"frame": 1, "drawCalls": 5}], "allocTracking": False, "params": params}
    if method == "profiler_dump":
        raise FakeError("not_recording", "the Chrome trace is not recording")
    if method == "zone_allocs":
        return {"enabled": False, "zones": [], "totals": {"count": 0}, "params": params}
    if method == "quit":
        raise QuitServer({"quitting": True})
    raise FakeError("unknown_method", method)


@pytest.fixture
def server():
    fake = FakeControlServer(echo_handler)
    fake.port = fake.start()
    yield fake
    fake.stop()


@pytest.fixture
def session():
    game = GameSession()
    yield game
    game.close()


def requests_of(server: FakeControlServer, method: str) -> list:
    return [params for name, params in server.requests if name == method]


def test_a_call_without_a_game_is_refused(session):
    with pytest.raises(SessionError, match="launch_game or connect"):
        session.call("get_state")


def test_connect_describes_the_game(session, server):
    description = session.connect(port=server.port)
    assert description["port"] == server.port
    assert description["status"] == {"frame": 7, "manualClock": True}
    assert description["activeGame"] == "Fake"
    assert session.is_connected


def test_a_call_passes_the_method_and_the_params_through(session, server):
    session.connect(port=server.port)
    reply = session.call("perform_action", {"type": "move", "uci": "e2e4"})
    assert reply["outcome"] == {"type": "move", "uci": "e2e4"}
    assert requests_of(server, "perform_action") == [{"type": "move", "uci": "e2e4"}]
    assert session.call("get_state")["state"] == {"board": "x"}


def test_a_refused_action_is_a_control_error_with_its_code(session, server):
    session.connect(port=server.port)
    with pytest.raises(ControlError) as refusal:
        session.call("perform_action", {"type": "bad"})
    assert refusal.value.code == "illegal_action"
    assert "illegal_action" in str(refusal.value)
    assert session.is_connected  # a refusal does not end the session
    assert session.call("status")["frame"] == 7


def test_step_sends_frames_and_leaves_out_dt_when_not_given(session, server):
    session.connect(port=server.port)
    session.step(3)
    session.step(2, 0.25)
    assert requests_of(server, "step") == [{"frames": 3}, {"frames": 2, "dt": 0.25}]


def test_screenshot_gives_the_decoded_png_and_its_size(session, server):
    session.connect(port=server.port)
    png, size = session.screenshot()
    assert png == b"\x89PNG-fake" and size == {"width": 2, "height": 1}
    session.screenshot(256)
    assert requests_of(server, "screenshot") == [None, {"maxSize": 256}]


def test_connecting_to_nothing_says_what_to_do(session):
    with pytest.raises(SessionError, match="launch_game"):
        session.connect(port=free_port())
    assert not session.is_connected


def test_quit_on_a_connected_game_ends_the_session(session, server):
    session.connect(port=server.port)
    assert session.quit() == {"quitting": True}  # a game that was only connected to has no exit code to report
    assert not session.is_connected
    with pytest.raises(SessionError, match="launch_game or connect"):
        session.call("get_state")


def test_the_game_going_away_mid_session_is_a_session_error(session):
    def handler(method: str, params: dict | None):
        if method == "status":
            return {"manualClock": True}
        if method == "list_games":
            return {"active": None, "games": []}
        raise QuitServer({"bye": True})

    fake = FakeControlServer(handler)
    port = fake.start()
    try:
        session.connect(port=port)
        session.call("anything")  # answered, and then the server drops the connection
        with pytest.raises(SessionError, match="closed the connection"):
            session.call("anything")
        assert not session.is_connected
    finally:
        fake.stop()


def test_a_call_that_takes_too_long_drops_the_connection(session):
    def handler(method: str, params: dict | None):
        if method == "slow":
            time.sleep(0.6)
        if method == "list_games":
            return {"active": None, "games": []}
        return {}

    fake = FakeControlServer(handler)
    port = fake.start()
    try:
        session.connect(port=port)
        with pytest.raises(SessionError, match="did not answer in time"):
            session.call("slow", timeout=0.2)
        assert not session.is_connected
    finally:
        fake.stop()


def test_a_game_that_was_only_connected_to_has_no_log(session, server):
    session.connect(port=server.port)
    with pytest.raises(SessionError, match="did not start the game"):
        session.game_log()


def test_launch_runs_the_game_headless_on_the_manual_clock(session, fake_game_exe):
    description = session.launch(str(fake_game_exe))
    assert description["activeGame"] == "Fake"
    assert description["status"]["manualClock"] is True
    assert session.call("get_state")["state"]["flags"] == ["--control", "--headless", "--manual-clock"]
    assert "listening on" in session.game_log()


def test_launch_can_ask_for_a_window_on_the_real_clock(session, fake_game_exe):
    session.launch(str(fake_game_exe), headless=False, manual_clock=False)
    assert session.call("get_state")["state"]["flags"] == ["--control"]
    assert session.call("status")["manualClock"] is False


def test_quit_stops_the_launched_game_and_reports_its_exit_code(session, fake_game_exe):
    session.launch(str(fake_game_exe))
    result = session.quit()
    assert result["quitting"] is True and result["exitCode"] == 0
    assert not session.is_connected


def test_launching_again_replaces_the_game(session, fake_game_exe):
    first = session.launch(str(fake_game_exe))["port"]
    second = session.launch(str(fake_game_exe))["port"]
    assert first != second
    assert session.call("status")["manualClock"] is True


def test_a_game_that_exits_at_once_is_reported_with_its_log(session, fake_game_exe, monkeypatch):
    monkeypatch.setenv("FAKE_GAME_EXIT", "1")
    with pytest.raises(SessionError, match="exited with code 3") as failure:
        session.launch(str(fake_game_exe))
    assert "exiting at once" in str(failure.value)
    assert not session.is_connected


def test_launch_of_a_missing_executable_names_the_path(session, tmp_path):
    with pytest.raises(SessionError, match="no game executable"):
        session.launch(str(tmp_path / "nothing"))


EXPECTED_TOOLS = {
    "launch_game", "connect", "status", "get_state", "list_actions", "perform_action", "set_state", "reset", "step", "screenshot", "ui_tree", "ui_click", "game_log", "quit",
    "frame_stats", "profiler_dump", "zone_allocs", "record_start", "record_stop",
}


def test_the_mcp_server_serves_the_tools_over_stdio(server):
    mcp = pytest.importorskip("mcp")

    async def scenario() -> None:
        parameters = mcp.StdioServerParameters(command=sys.executable, args=[str(TOOL_DIR / "bagi_mcp.py")])
        async with mcp.Client(parameters) as client:
            listed = await client.list_tools()
            assert {tool.name for tool in listed.tools} == EXPECTED_TOOLS

            connected = await client.call_tool("connect", {"port": server.port})
            assert not connected.is_error

            state = await client.call_tool("get_state", {})
            assert not state.is_error
            assert "board" in state.content[0].text

            moved = await client.call_tool("perform_action", {"action": {"type": "move", "uci": "e2e4"}})
            assert not moved.is_error and "e2e4" in moved.content[0].text

            refused = await client.call_tool("perform_action", {"action": {"type": "bad"}})
            assert refused.is_error and "illegal_action" in refused.content[0].text

            picture = await client.call_tool("screenshot", {"max_size": 64})
            assert not picture.is_error
            assert picture.content[0].type == "image" and picture.content[0].mime_type == "image/png"
            assert base64.b64decode(picture.content[0].data) == b"\x89PNG-fake"

            stepped = await client.call_tool("step", {"frames": 2})
            assert not stepped.is_error

            tree = await client.call_tool("ui_tree", {})
            assert not tree.is_error and "canvases" in tree.content[0].text

            clicked = await client.call_tool("ui_click", {"name": "Ok"})
            assert not clicked.is_error and "Ok" in clicked.content[0].text
            within = await client.call_tool("ui_click", {"name": "Ok", "canvas": "Debug"})
            assert not within.is_error

            slid = await client.call_tool("ui_click", {"name": "Volume", "at": 0.25})
            assert not slid.is_error

            missing = await client.call_tool("ui_click", {"name": "Nothing"})
            assert missing.is_error and "not_found" in missing.content[0].text

            stats = await client.call_tool("frame_stats", {"frames": 3})
            assert not stats.is_error and "drawCalls" in stats.content[0].text
            dumped = await client.call_tool("profiler_dump", {})
            assert dumped.is_error and "not_recording" in dumped.content[0].text
            zones = await client.call_tool("zone_allocs", {"top": 5, "reset": True})
            assert not zones.is_error and "totals" in zones.content[0].text

    asyncio.run(scenario())
    assert requests_of(server, "screenshot")[-1] == {"maxSize": 64}
    assert requests_of(server, "step")[-1] == {"frames": 2}
    assert requests_of(server, "frame_stats")[-1] == {"frames": 3}
    assert requests_of(server, "profiler_dump")[-1] == {"path": "trace.json"}
    assert requests_of(server, "zone_allocs")[-1] == {"top": 5, "reset": True}
    assert requests_of(server, "ui_click")[:3] == [{"name": "Ok"}, {"name": "Ok", "canvas": "Debug"}, {"name": "Volume", "at": 0.25}]


def test_every_mcp_tool_is_wrapped_in_reports_errors():
    pytest.importorskip("mcp")
    import bagi_mcp

    tools = bagi_mcp.server._tool_manager.list_tools()  # no public list outside the async protocol
    assert {tool.name for tool in tools} == EXPECTED_TOOLS
    # reports_errors uses functools.wraps, which leaves __wrapped__ on the function the server calls
    assert [tool.name for tool in tools if not hasattr(tool.fn, "__wrapped__")] == []


def test_the_mcp_server_records_a_session_as_a_scenario(fake_game_exe, tmp_path):
    mcp = pytest.importorskip("mcp")
    target = tmp_path / "bug.jsonl"

    async def run() -> None:
        parameters = mcp.StdioServerParameters(command=sys.executable, args=[str(TOOL_DIR / "bagi_mcp.py")])
        async with mcp.Client(parameters) as client:
            launched = await client.call_tool("launch_game", {"exe": str(fake_game_exe)})
            assert not launched.is_error

            started = await client.call_tool("record_start", {"path": str(target)})
            assert not started.is_error and "bug.jsonl" in started.content[0].text

            again = await client.call_tool("record_start", {"path": str(target)})
            assert again.is_error and "already recording" in again.content[0].text

            assert not (await client.call_tool("get_state", {})).is_error
            assert not (await client.call_tool("perform_action", {"action": {"type": "noop"}})).is_error
            refused = await client.call_tool("perform_action", {"action": {"type": "bad"}})
            assert refused.is_error and "illegal_action" in refused.content[0].text

            stopped = await client.call_tool("record_stop", {})
            assert not stopped.is_error
            assert "bug.jsonl" in stopped.content[0].text and '"calls": 3' in stopped.content[0].text

            missing = await client.call_tool("record_stop", {})
            assert missing.is_error and "not recording" in missing.content[0].text
            assert "record_start" in missing.content[0].text

            assert not (await client.call_tool("quit", {})).is_error

    asyncio.run(run())
    header, steps = scenario.load(target)
    assert header.game == "Fake" and header.flags == ["--headless", "--manual-clock"]
    assert [step.method for step in steps] == ["get_state", "perform_action", "perform_action"]
    assert steps[2].expect == {"error": "illegal_action"}
