---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "tools/control/**/*"
  - "tests/BECore/Control*"
---

# Starting the control API (flags, config, status)

The module: `src/Modules/BECore/Control/CLAUDE.md`.

The API is off by default: a game has to be asked for it. `Application::Initialize()` loads the reflected `ApplicationConfig` (`LoadApplicationConfig()`) and calls `ResolveControlSettings(args, load)` right after the core services are up (before the window exists) and, when the API is enabled, starts the `RemoteControl` server after `GameManager::Initialize()`, so the game methods are registered before the first client connects. A panel can ask for it later (`ApplicationEvents::StartControlServerRequestEvent`, the Generate panel): the default port, else a free one; a failure is logged.

| Command line | `ApplicationConfig.xml` root attribute | Meaning |
|--------------|----------------------------------------|---------|
| `--control` | `control="true"` (or `false`, `1`, `0`) | start the WebSocket server |
| `--control-port=N` | `controlPort="N"` | port 0 to 65535; 0 asks the system for a free one; default `kDefaultControlPort` (8765) |
| `--headless` | `headless="true"` (or `false`, `1`, `0`) | no window: the SDL software renderer on the dummy video driver, a screenshot is the way to see the game (`SDL/CLAUDE.md`, "Headless and screenshots"); needs the API and the Game run mode |
| `--window=WxH`, `--density=D` | none (flags only) | the start size (window units) and pixel density of a headless window, within `WindowLimits`; need `--headless` (`.claude/rules/control-window-api.md`) |
| `--manual-clock` | `manualClock="true"` (or `false`, `1`, `0`) | frames run only on `step` (see "Manual clock and `step`"); needs the API. The flag only turns it on, so a game that has `manualClock="true"` in its config cannot switch it off from the command line |
| `--replay=FILE`, `--replay-exit` | none | plays a replay file once the game is up; exit code 0 or 1, and `--replay-exit` also quits; both need the API and the manual clock (`Games/CLAUDE.md`, "Record and replay") |

`Application::PublishControlStatus()` copies the server's state (`IsRunning()`, `GetPort()`, `WebSocketServer::GetClientCount()`) into `ControlService::SetServerStatus()` every frame, the idle pass of the manual clock included, and `Cleanup` resets it. Main thread only; the editor's status bar reads `GetServerStatus()`. A build without `BE_WITH_REMOTE_CONTROL`, or with the API off, never sets it (`listening == false`).

The file half is the reflected `ApplicationConfig` read with strict values (`reflection-module.md`): an attribute that is not a boolean or not a number of the field's type is an error of the load under the attribute's name, and the error of one of the four control attributes stops the start-up (also when the API is off or a flag would override it). Errors of the other fields (`mode`, `type`) are not control errors: `Application::Initialize()` logs them once after the settings were resolved. The flags are read through `CommandLine`: a repeated flag takes its first value, so a flag appended to a launcher's command (`tools/control/game_process.py`) cannot move its port. The command line beats the attribute, the attribute beats the default. Unknown arguments are ignored (a game may have flags of its own). A value that cannot be read, a bare `--control-port`, and `--control-port=N`, the manual clock or headless mode while the API stays off, and `--window` / `--density` without `--headless` are errors: the start-up stops with a line in the log (`Application::Initialize()` flushes it itself, nothing else does before the first frame), so nobody who asked for the API is left waiting. A port that is taken stops it the same way. What headless, the window size and the density mean for the window reaches the SDL module as `ApplicationOptions` (`ApplicationFabric::Create(config, options)`), not as ControlSettings, which it does not see.
