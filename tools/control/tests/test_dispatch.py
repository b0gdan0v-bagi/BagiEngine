"""The session behind the MCP tools (bagi_session.py) against a fake control API, and the MCP server over stdio."""

import asyncio
import base64
import json
import os
import sys
import time
from pathlib import Path

import pytest

import scenario
from bagi_control_client import ControlError
from bagi_session import CALL_TIMEOUT_SECONDS, STEP_SECONDS_PER_FRAME, GameSession, SessionError
from fake_control_server import FakeControlServer, FakeError, QuitServer
from fake_game import png_2x1
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
    if method == "set_window":
        return {"width": (params or {}).get("width"), "params": params}
    if method == "ui_click":
        if (params or {}).get("name") == "Nothing":
            raise FakeError("not_found", "no element named 'Nothing'")
        return {"clicked": (params or {}).get("name"), "params": params}
    if method == "click_at":
        if not isinstance((params or {}).get("x"), (int, float)):
            raise FakeError("invalid_params", "'x' and 'y' have to be finite numbers")
        return {"consumed": False, "params": params}
    if method == "frame_stats":
        return {"frames": [{"frame": 1, "drawCalls": 5}], "allocTracking": False, "params": params}
    if method == "profiler_dump":
        raise FakeError("not_recording", "the Chrome trace is not recording")
    if method == "zone_allocs":
        return {"enabled": False, "zones": [], "totals": {"count": 0}, "params": params}
    if method == "editor_state":
        return {"anyDirty": False, "documents": []}
    if method == "log_mark":
        return {"mark": 5, "warnings": 1, "errors": 0}
    if method == "log_since":
        if (params or {}).get("mark", -1) < 0:
            raise FakeError("invalid_params", "'mark' has to be an integer, 0 or more")
        return {"lines": [], "warnings": 0, "errors": 0, "dropped": 0, "next": 5, "params": params}
    if method == "log_subscribe":
        return {"enabled": (params or {}).get("enabled"), "level": (params or {}).get("level"), "params": params}
    if method == "batch":
        return {"results": [{"ok": True, "result": {}}], "params": params}
    if method == "run_until":
        return {"met": False, "frames": (params or {}).get("maxFrames"), "params": params}
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


def test_a_step_called_through_call_gets_the_timeout_of_its_frames(session, server, monkeypatch):
    session.connect(port=server.port)
    seen = []
    client = session._client
    real_call = client.call

    def spy(method, params=None):
        seen.append((method, client.timeout))
        return real_call(method, params)

    monkeypatch.setattr(client, "call", spy)
    session.call("step", {"frames": 1000})
    session.call("get_state")
    assert seen == [("step", CALL_TIMEOUT_SECONDS + 1000 * STEP_SECONDS_PER_FRAME), ("get_state", CALL_TIMEOUT_SECONDS)]


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


def test_launch_starts_the_window_at_a_size_and_a_density(session, fake_game_exe):
    description = session.launch(str(fake_game_exe), window="390x844", density=3)
    window = description["status"]["window"]
    assert (window["width"], window["height"], window["density"]) == (390, 844, 3.0)
    assert (window["outputWidth"], window["outputHeight"]) == (1170, 2532)
    assert session.call("get_state")["state"]["flags"] == ["--control", "--headless", "--manual-clock"]


def test_launch_without_a_window_flag_leaves_the_size_of_the_game(session, fake_game_exe):
    window = session.launch(str(fake_game_exe))["status"]["window"]
    assert window["density"] == 1.0 and window["safeArea"] == {"top": 0.0, "right": 0.0, "bottom": 0.0, "left": 0.0}


@pytest.mark.parametrize("window", ["480", "480X480", "480x", "0x0x1", "480 x 480", "-1x5", "480.5x480"])
def test_launch_refuses_a_window_that_is_not_wxh(session, fake_game_exe, window):
    with pytest.raises(SessionError, match="WxH"):
        session.launch(str(fake_game_exe), window=window)
    assert not session.is_connected


def test_launch_refuses_a_window_or_density_without_headless(session, fake_game_exe):
    with pytest.raises(SessionError, match="headless"):
        session.launch(str(fake_game_exe), headless=False, window="480x480")
    with pytest.raises(SessionError, match="headless"):
        session.launch(str(fake_game_exe), headless=False, density=2)
    assert not session.is_connected


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


def test_set_window_sends_only_what_it_is_given(session, server):
    session.connect(port=server.port)
    session.set_window(480, 480)
    session.set_window(300, 200, density=2)
    session.set_window(300, 200, user_scale=1.25)
    session.set_window(300, 200, density=0.5, safe_area={"top": 40, "right": 0, "bottom": 0, "left": 0})
    assert requests_of(server, "set_window") == [
        {"width": 480, "height": 480},
        {"width": 300, "height": 200, "density": 2},
        {"width": 300, "height": 200, "userScale": 1.25},
        {"width": 300, "height": 200, "density": 0.5, "safeArea": {"top": 40, "right": 0, "bottom": 0, "left": 0}},
    ]


def test_a_safe_area_tuple_is_top_right_bottom_left(session, server):
    session.connect(port=server.port)
    session.set_window(480, 480, safe_area=(1, 2, 3, 4))
    session.set_window(480, 480, safe_area=[0, 0, 0, 0])
    assert [params["safeArea"] for params in requests_of(server, "set_window")] == [{"top": 1, "right": 2, "bottom": 3, "left": 4}, dict.fromkeys(("top", "right", "bottom", "left"), 0)]


def test_a_safe_area_of_the_wrong_length_is_refused_before_a_call(session, server):
    session.connect(port=server.port)
    with pytest.raises(SessionError, match="top, right, bottom, left"):
        session.set_window(480, 480, safe_area=(1, 2, 3))
    assert requests_of(server, "set_window") == []


def test_set_window_without_a_game_is_refused(session):
    with pytest.raises(SessionError, match="launch_game or connect"):
        session.set_window(480, 480)


EXPECTED_TOOLS = {
    "launch_game", "connect", "status", "get_state", "list_actions", "perform_action", "set_state", "reset", "step", "screenshot", "ui_tree", "ui_click", "click_at", "game_log", "quit",
    "frame_stats", "profiler_dump", "zone_allocs", "record_start", "record_stop", "image_diff",
    "log_mark", "log_since", "log_subscribe", "batch", "run_until", "set_window", "editor_state",
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

            resized = await client.call_tool("set_window", {"width": 390, "height": 844})
            assert not resized.is_error
            dense = await client.call_tool("set_window", {"width": 390, "height": 844, "density": 3.0, "user_scale": 1.5, "safe_area": [47, 0, 34, 0]})
            assert not dense.is_error

            clicked = await client.call_tool("ui_click", {"name": "Ok"})
            assert not clicked.is_error and "Ok" in clicked.content[0].text
            within = await client.call_tool("ui_click", {"name": "Ok", "canvas": "Debug"})
            assert not within.is_error

            slid = await client.call_tool("ui_click", {"name": "Volume", "at": 0.25})
            assert not slid.is_error

            missing = await client.call_tool("ui_click", {"name": "Nothing"})
            assert missing.is_error and "not_found" in missing.content[0].text

            point = await client.call_tool("click_at", {"x": 10, "y": 20})
            assert not point.is_error and "consumed" in point.content[0].text
            right = await client.call_tool("click_at", {"x": 1.5, "y": 2, "space": "scene", "button": "right", "which": 3})
            assert not right.is_error
            badpoint = await client.call_tool("click_at", {"x": "left", "y": 2})
            assert badpoint.is_error

            stats = await client.call_tool("frame_stats", {"frames": 3})
            assert not stats.is_error and "drawCalls" in stats.content[0].text
            dumped = await client.call_tool("profiler_dump", {})
            assert dumped.is_error and "not_recording" in dumped.content[0].text
            zones = await client.call_tool("zone_allocs", {"top": 5, "reset": True})
            assert not zones.is_error and "totals" in zones.content[0].text

            editor = await client.call_tool("editor_state", {})
            assert not editor.is_error and '"anyDirty": false' in editor.content[0].text

            marked = await client.call_tool("log_mark", {})
            assert not marked.is_error and '"mark": 5' in marked.content[0].text
            since = await client.call_tool("log_since", {"mark": 5, "level": "error"})
            assert not since.is_error and "dropped" in since.content[0].text
            refused_mark = await client.call_tool("log_since", {"mark": -1})
            assert refused_mark.is_error and "invalid_params" in refused_mark.content[0].text
            subscribed = await client.call_tool("log_subscribe", {"enabled": True})
            assert not subscribed.is_error and '"enabled": true' in subscribed.content[0].text
            ran = await client.call_tool("batch", {"calls": [{"method": "ping"}, {"method": "step", "params": {"frames": 2}}], "continue_on_error": True})
            assert not ran.is_error and "results" in ran.content[0].text
            waited = await client.call_tool("run_until", {"pointer": "/state/score", "equals": 3, "max_frames": 7, "dt": 0.5})
            assert not waited.is_error and '"met": false' in waited.content[0].text
            moved_version = await client.call_tool("run_until", {"version_moved": True})
            assert not moved_version.is_error
            happened = await client.call_tool("run_until", {"event": "state_changed", "event_equal": {"version": 2}, "max_frames": 4})
            assert not happened.is_error

    asyncio.run(scenario())
    assert requests_of(server, "screenshot")[-1] == {"maxSize": 64}
    assert requests_of(server, "step")[-1] == {"frames": 2}
    assert requests_of(server, "frame_stats")[-1] == {"frames": 3}
    assert requests_of(server, "profiler_dump")[-1] == {"path": "trace.json"}
    assert requests_of(server, "zone_allocs")[-1] == {"top": 5, "reset": True}
    assert requests_of(server, "click_at")[:2] == [{"x": 10, "y": 20}, {"x": 1.5, "y": 2, "space": "scene", "button": "right", "which": 3}]
    assert requests_of(server, "log_since")[0] == {"mark": 5, "level": "error"}
    assert requests_of(server, "log_subscribe") == [{"enabled": True, "level": "warning"}]
    assert requests_of(server, "batch") == [{"calls": [{"method": "ping"}, {"method": "step", "params": {"frames": 2}}], "continueOnError": True}]
    assert requests_of(server, "run_until") == [
        {"condition": {"pointer": "/state/score", "equals": 3}, "maxFrames": 7, "dt": 0.5},
        {"condition": {"versionMoved": True}, "maxFrames": 600},
        {"condition": {"event": {"name": "state_changed", "equal": {"version": 2}}}, "maxFrames": 4},
    ]
    assert requests_of(server, "set_window") == [
        {"width": 390, "height": 844},
        {"width": 390, "height": 844, "density": 3.0, "userScale": 1.5, "safeArea": {"top": 47, "right": 0, "bottom": 34, "left": 0}},
    ]
    assert requests_of(server, "ui_click")[:3] == [{"name": "Ok"}, {"name": "Ok", "canvas": "Debug"}, {"name": "Volume", "at": 0.25}]


def test_the_mcp_run_until_refuses_none_or_two_conditions_without_a_call(server):
    mcp = pytest.importorskip("mcp")

    async def run() -> None:
        parameters = mcp.StdioServerParameters(command=sys.executable, args=[str(TOOL_DIR / "bagi_mcp.py")])
        async with mcp.Client(parameters) as client:
            assert not (await client.call_tool("connect", {"port": server.port})).is_error
            none = await client.call_tool("run_until", {})
            assert none.is_error and "exactly one condition" in none.content[0].text and "none" in none.content[0].text
            two = await client.call_tool("run_until", {"pointer": "/version", "equals": 1, "version_moved": True})
            assert two.is_error and "pointer, version_moved" in two.content[0].text
            alone = await client.call_tool("run_until", {"pointer": "/version", "event_equal": {"a": 1}, "version_moved": False, "event": "x"})
            assert alone.is_error
            stray = await client.call_tool("run_until", {"version_moved": True, "event_equal": {"a": 1}})
            assert stray.is_error and "event_equal" in stray.content[0].text

    asyncio.run(run())
    assert requests_of(server, "run_until") == []


def test_the_mcp_image_diff_answers_stats_and_an_image_and_errors_as_text(tmp_path):
    mcp = pytest.importorskip("mcp")
    reference = tmp_path / "reference.png"
    reference.write_bytes(png_2x1())
    other = tmp_path / "other.png"
    other.write_bytes(png_2x1())

    def game(method: str, params: dict | None):
        if method == "status":
            return {"frame": 1, "manualClock": True}
        if method == "list_games":
            return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
        if method == "screenshot":
            return {"width": 2, "height": 1, "png_base64": base64.b64encode(png_2x1()).decode("ascii")}
        raise FakeError("unknown_method", method)

    fake = FakeControlServer(game)
    port = fake.start()

    async def run() -> None:
        parameters = mcp.StdioServerParameters(command=sys.executable, args=[str(TOOL_DIR / "bagi_mcp.py")])
        async with mcp.Client(parameters) as client:
            files = await client.call_tool("image_diff", {"a": str(reference), "b": str(other)})  # no game connected: two files need none
            assert not files.is_error
            assert [part.type for part in files.content] == ["text", "image"]
            assert json.loads(files.content[0].text)["equal"] is True
            assert files.content[1].mime_type == "image/png"

            assert not (await client.call_tool("connect", {"port": port})).is_error
            screen = await client.call_tool("image_diff", {"a": str(reference), "b": "screen", "region": [0, 0, 1, 0], "threshold": 3})
            assert not screen.is_error
            stats = json.loads(screen.content[0].text)
            assert stats["equal"] is True and stats["region"] == [0, 0, 1, 0] and stats["threshold"] == 3

            missing = await client.call_tool("image_diff", {"a": str(reference), "b": str(tmp_path / "nothing.png")})
            assert missing.is_error and "nothing.png" in missing.content[0].text
            bad = await client.call_tool("image_diff", {"a": str(reference), "b": str(other), "region": [0, 0, 9, 9]})
            assert bad.is_error and "region" in bad.content[0].text

    try:
        asyncio.run(run())
    finally:
        fake.stop()
    assert [params for name, params in fake.requests if name == "screenshot"] == [{"maxSize": 4096}]


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
