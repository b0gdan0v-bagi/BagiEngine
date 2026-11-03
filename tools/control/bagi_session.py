"""The game session behind the MCP tools: starts or reaches one game and turns its control API into plain Python calls.

No MCP code in here (bagi_mcp.py maps these methods onto tools), so it runs and is tested without the `mcp` package.
Standard library only.

    session = GameSession()
    session.launch("build/ClangmacOS/BagiMatch3")
    session.call("perform_action", {"type": "swap", "from": {"col": 0, "row": 0}, "to": {"col": 1, "row": 0}})
    png, size = session.screenshot()
    session.quit()

An error that the game answers is raised as ControlError (its text is "code: message"); everything the session itself
refuses (no game, a lost connection, a missing executable) is a SessionError.
"""

import base64
from pathlib import Path
from typing import Any

from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port

CALL_TIMEOUT_SECONDS = 60.0
STEP_SECONDS_PER_FRAME = 0.05  # a generous bound for a frame: a step of N frames may take N times this before the call gives up
LOG_LINES = 40


class SessionError(Exception):
    """The session cannot do what was asked; the message says what to do instead."""


class GameSession:
    """At most one game at a time, either started by launch() or reached by connect()."""

    def __init__(self) -> None:
        self._client: BagiControlClient | None = None
        self._game: GameProcess | None = None

    @property
    def is_connected(self) -> bool:
        return self._client is not None

    def launch(self, exe: str, headless: bool = True, manual_clock: bool = True, startup_timeout: float = DEFAULT_STARTUP_TIMEOUT) -> dict[str, Any]:
        """Starts `exe` with the control API on a free port and connects to it. A game that this session started earlier is stopped."""
        self.close()
        exe_path = Path(exe).expanduser().resolve()
        if not exe_path.is_file():
            raise SessionError(f"there is no game executable at {exe_path}")

        port = free_port()
        game = GameProcess(build_game_command(exe_path, port, headless, manual_clock), cwd=exe_path.parent)
        try:
            game.start()
            client = game.connect(port, startup_timeout)
        except GameLaunchError as error:
            game.stop(0)
            raise SessionError(str(error)) from error
        client.timeout = CALL_TIMEOUT_SECONDS
        self._game, self._client = game, client
        return self._describe(port)

    def connect(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> dict[str, Any]:
        """Connects to a game that is already running with --control. A game that this session started earlier is stopped."""
        self.close()
        client = BagiControlClient(host=host, port=port, timeout=CALL_TIMEOUT_SECONDS)
        try:
            client.connect()
        except OSError as error:
            raise SessionError(f"nothing accepts a control connection at {host}:{port} ({error}); start the game with --control or use launch_game") from error
        self._client = client
        return self._describe(port)

    def call(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> Any:
        """One control call. `timeout` (seconds) replaces the default for a call that is known to take long."""
        client = self._client
        if client is None:
            raise SessionError("no game is connected: call launch_game or connect first")
        previous_timeout = client.timeout
        if timeout is not None:
            client.timeout = timeout
        try:
            return client.call(method, params)
        except ConnectionClosed as error:
            self._drop_client()
            raise SessionError(f"the game closed the connection ({error}); {self._exit_note()}") from error
        except TimeoutError as error:
            # The answer may still come and would be taken for the answer of the next call, so this connection is done
            self._drop_client()
            raise SessionError("the game did not answer in time; the connection was dropped (use connect to reach the game again)") from error
        except OSError as error:
            self._drop_client()
            raise SessionError(f"lost the connection to the game ({error})") from error
        finally:
            client.timeout = previous_timeout
            client.events.clear()  # pushes are not used here and would pile up

    def step(self, frames: int = 1, dt: float | None = None) -> Any:
        """Runs `frames` frames of `dt` seconds on the manual clock (on the real clock it waits for that many frames)."""
        params: dict[str, Any] = {"frames": frames}
        if dt is not None:
            params["dt"] = dt
        timeout = CALL_TIMEOUT_SECONDS + max(frames, 0) * STEP_SECONDS_PER_FRAME
        return self.call("step", params, timeout=timeout)

    def screenshot(self, max_size: int | None = None) -> tuple[bytes, dict[str, int]]:
        """The PNG of the next frame and its size. `max_size` shrinks the longer side of a bigger picture (the game's default is 1024)."""
        result = self.call("screenshot", None if max_size is None else {"maxSize": max_size})
        return base64.b64decode(result["png_base64"]), {"width": result["width"], "height": result["height"]}

    def game_log(self, lines: int = LOG_LINES) -> str:
        """The end of the output of the game this session started; a game it only connected to has none."""
        if self._game is None:
            raise SessionError("this session did not start the game, so it has no log; launch_game starts one")
        return self._game.log_tail(lines)

    def quit(self) -> dict[str, Any]:
        """Asks the game to quit, waits for the process (a launched one) and forgets it."""
        result: dict[str, Any] = {}
        if self._client is not None:
            try:
                result = self.call("quit") or {}
            except (ControlError, SessionError):
                pass  # it is going away either way
        exit_code = self._stop_game(DEFAULT_SHUTDOWN_TIMEOUT)
        self._drop_client()
        if exit_code is not None:
            result["exitCode"] = exit_code
        return result

    def close(self) -> None:
        """Lets go of the game: a launched one is killed at once, one that was only connected to keeps running."""
        self._drop_client()
        self._stop_game(0)

    def _describe(self, port: int) -> dict[str, Any]:
        games = self.call("list_games")
        return {"port": port, "status": self.call("status"), "activeGame": games.get("active"), "games": games.get("games", [])}

    def _drop_client(self) -> None:
        client, self._client = self._client, None
        if client is not None:
            try:
                client.close()
            except (OSError, ConnectionClosed):
                pass

    def _stop_game(self, timeout: float) -> int | None:
        game, self._game = self._game, None
        return None if game is None else game.stop(timeout)

    def _exit_note(self) -> str:
        if self._game is None:
            return "start it again with launch_game or connect"
        code = self._game.exit_code
        return "the process is still running" if code is None else f"the process exited with code {code}, game_log has its last lines"
