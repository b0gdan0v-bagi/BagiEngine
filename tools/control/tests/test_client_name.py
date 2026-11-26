"""The name a control connection gives the engine (`client_hello`): bagi_control_client.py, bagi_session.py, bagi_mcp.py."""

import pytest

import scenario
from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError
from bagi_session import GameSession
from fake_control_server import FakeControlServer, FakeError


@pytest.fixture
def serve():
    """Starts a fake engine that answers `client_hello` with `hello` (a result, or a FakeError to raise) and gives (server, port)."""
    servers = []

    def start(hello=None):
        def handle(method, params):
            if method == "client_hello":
                if isinstance(hello, FakeError):
                    raise hello
                return {"id": 1, "name": params["name"]}
            if method == "list_games":
                return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
            if method == "status":
                return {"manualClock": True}
            return {"method": method}

        server = FakeControlServer(handle)
        servers.append(server)
        return server, server.start()

    yield start
    for server in servers:
        server.stop()


def test_a_named_client_says_hello_once_per_connection_and_an_unnamed_one_sends_nothing(serve):
    server, port = serve()
    with BagiControlClient(port=port) as client:
        client.call("ping")
    with BagiControlClient(port=port, name="tool") as client:
        client.call("ping")
    assert server.requests == [("ping", None), ("client_hello", {"name": "tool"}), ("ping", None)]


def test_an_engine_without_client_hello_is_not_an_error(serve):
    server, port = serve(FakeError("unknown_method", "unknown method 'client_hello'"))
    with BagiControlClient(port=port, name="tool") as client:
        assert client.call("ping") == {"method": "ping"}
    assert [method for method, _ in server.requests] == ["client_hello", "ping"]


def test_another_refusal_of_the_name_fails_the_connect_and_leaves_no_connection(serve):
    _, port = serve(FakeError("invalid_params", "'name' has to be shorter"))
    client = BagiControlClient(port=port, name="tool")
    with pytest.raises(ControlError) as refused:
        client.connect()
    assert refused.value.code == "invalid_params"
    with pytest.raises(ConnectionClosed):
        client.call("ping")


def test_a_session_passes_its_name_on_and_a_recording_never_holds_the_hello(serve, tmp_path):
    server, port = serve()
    path = tmp_path / "play.jsonl"
    session = GameSession(client_name="agent")
    try:
        session.connect(port=port, record=str(path))
        session.call("perform_action", {"type": "inc"})
        session.stop_recording()
    finally:
        session.close()
    assert server.requests[0] == ("client_hello", {"name": "agent"})
    _, steps = scenario.load(path)
    assert [step.method for step in steps] == ["perform_action"]


def test_the_mcp_server_names_its_session(serve):
    bagi_mcp = pytest.importorskip("bagi_mcp")
    server, port = serve()
    try:
        bagi_mcp.session.connect(port=port)
    finally:
        bagi_mcp.session.close()
    assert server.requests[0] == ("client_hello", {"name": "mcp"})
