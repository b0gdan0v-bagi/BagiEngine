# Control Module Guidelines

## Overview

The core of the external control API: a program or a model sends commands, the engine runs them on the main thread. This directory has no transport and no game knowledge. A transport (the WebSocket server of the `RemoteControl` module) feeds `ControlService::Post()`; modules above BECore register the methods (`ping`, `status`, `step`, `quit` live here; the game methods are registered by `Games`, the screenshot by `SDL`). What is left to do (a capture on Vulkan, the Stockfish games in ctest or CI, a person at the window) is in the entry `control-api-gaps` of the todo store; the tools that use the API are in `tools/control/CLAUDE.md`.

| File | Purpose |
|------|---------|
| `ControlService` | Thread-safe FIFO queue of requests, handler table, barrier, deferred answers, push events. One instance: `CoreManager::GetControlService()` |
| `ControlCall` | One request being handled: `Params()`, `Reply()`, `Fail()`, `DeferFrames()` |
| `ControlBuiltins` | `RegisterControlBuiltins()`: `ping`, `status`, `step`, `quit` |
| `ControlSettings` | `ResolveControlSettings(args, ApplicationConfigLoad)`: the start-up flags and the settings of the reflected `ApplicationConfig` (`Application/ApplicationConfig.h`), the manual clock included (see "Starting the API"), a pure function |
| `ControlTypes.h` | `ControlError`, `ControlErrors::k*` (error codes), `ControlReplyFn` |
| `Json.h`, `JsonFwd.h` | nlohmann/json behind checked helpers (see "JSON rules") |
| `Utils/Base64.h` (one level up) | Base64 for the screenshot and the WebSocket handshake |

## Protocol

JSON text, one message per request.

```
request   {"id": 1, "method": "perform_action", "params": {...}}     id: integer or string, required; params: object, optional
response  {"id": 1, "ok": true,  "result": {...}}
          {"id": 1, "ok": false, "error": {"code": "illegal_action", "message": "..."}}
push      {"event": "state_changed", "data": {"game": "Match3", "version": 12, "state": {...}}}
```

Error codes are strings and part of the protocol (`ControlErrors`): `parse_error`, `invalid_request`, `invalid_params`, `unknown_method`, `request_too_large`, `internal_error`, `shutting_down`. A method adds its own (`illegal_action`, ...). A request that cannot be parsed gets `"id": null`. `ping` reports `ControlService::kProtocolVersion`; change it when the format changes incompatibly.

`state_changed` is the only push so far (`GameControl` publishes it, `Games/CLAUDE.md`). It is sent at the end of the frame in which the state version of the active game moved, whoever moved it: a client, a reset, a click of a person. `data` is what `get_state` answers (`game`, `version`, `state`; `state` is left out when the game cannot give it). The first frame only records the version, so nothing is pushed for the state a client connects to. An action that a client makes with the manual clock waits for the next frame like everything else (see "Manual clock and `step`").

## Frame and threads

- `Post()` is the only thread-safe call. It parses the text on the posting thread and queues the request; everything else (`RegisterHandler`, `Drain`, `EndFrame`, `Publish`, `Shutdown`, the handlers, the reply and the event sink) belongs to the main thread.
- `CoreManager::OnGameCycle` calls `Drain()` as stage **1b**: after input, before `TaskManager::Update`. A command therefore sees this frame's input and what it changes is seen by this frame's logic and render. `Application::RunFrame` calls `EndFrame()` after the present.
- Requests run in the order they were posted (per posting thread). Do not use `TaskManager` for commands: its main-thread queue is a priority queue and does not keep the order of equal priorities.
- A handler ends in exactly one of `Reply()`, `Fail()`, `DeferFrames()`; one that does none gets `internal_error` and an error log line.
- `DeferFrames(n, done)` answers later: `done` runs at the end of the frame in which the counter (starting at `n`, at least 1) reaches zero. `DeferFrames(n, secondsPerFrame, done)` also says how long each frame lasts, which only the manual clock uses (negative or not a number is zero; the default is `kDefaultStepSeconds`, 1/60). While a call is deferred it is a **barrier**: the requests behind it wait, so a client that pipelines `step` and `get_state` gets the state after the step. A method registered with `bypassBarrier` (`quit`) still runs, but cannot defer itself.
- `SetFrameEndHook(name, hook)` runs `hook` at every `EndFrame()`, before a deferred call completes (the state-change push uses it). Setting a name again replaces its hook, so a module that registers twice (`GameManager::Initialize()` does, once per test) adds nothing. A hook that sets another one is fine: the new one runs in the same frame.
- `Shutdown()` (from `OnApplicationDeinit`) answers everything queued or deferred with `shutting_down`; a request posted later is answered at once on the posting thread. Stop the transport before it.
- Never use `ASSERT` / `EXPECT` / `FATALERROR` on data from a client: a bad request is an error response, not a bug in the engine.
- Method names live in an `eastl::hash_map<eastl::string, ...>`, not in `PoolString`: a client must not be able to grow the pool (`ControlServiceTest` checks this with `PoolString::Find`).

## JSON rules

The engine builds without exceptions, so nlohmann/json is built with `JSON_NOEXCEPTION` and **aborts the whole process** where it would throw. Checked on this build: `at()` on a missing key, `get<T>()` on the wrong type, `value()` on a non-object and `operator[]` by a missing key on a const object all abort.

- Include `BECore/Control/Json.h` in `.cpp` files and in headers that really need the complete type (`ControlCall.h`, a game's adapter). Headers that only mention the type include `JsonFwd.h`. `CoreManager.h` must not pull in the full header (about 900 KB); it is not in the PCH either.
- Parse client text with `ParseJson()` only (it uses `allow_exceptions = false`). Read members with `FindJsonMember()`, `GetJsonString/Int/Number/Bool()`, which check the type. `GetJsonInt` rejects `1.5`, `"3"` and numbers that do not fit `int64_t`.
- Never call `at()`, `get<T>()`, `value()` or the const `operator[]` on client data. Building a response (`Json::object()`, `result["x"] = ...`) and `DumpJson()` are safe; `DumpJson()` replaces invalid UTF-8 instead of aborting.
- Keep the 1 MiB request limit (`ControlService::kMaxRequestBytes`) at every transport: it bounds the nesting that a client can send.
- nlohmann uses `std::string` inside; convert to `eastl::string` at the edge, which the helpers do.

## Manual clock and `step`

`step {"frames": N, "dt": seconds}` is a deferred call: it answers when `N` frames (1 to `kMaxStepFrames`, default 1) have ended, with the clock as `status` reports it (`frame`, `totalTime`, `deltaTime`, `fixedDeltaTime`). `dt` is 0 to `Time::kMaxFrameTime` (default 1/60). Both are checked as client data: out of range, fractional (`frames: 1.5`), the wrong type or `null` is `invalid_params`.

The two clocks differ in who runs the frames:

| | Real clock (default) | Manual clock (`--manual-clock`) |
|---|---|---|
| Frames | run all the time; `Time::Tick` measures them | run only while `ControlService::IsStepDue()`; `Time::Advance(GetStepSeconds())` sets their length |
| `step` | waits for `N` frames; `dt` is ignored | makes `N` frames of `dt` seconds, then answers |
| Between steps | | `Application::RunFrame` calls `CoreManager::OnIdleCycle`: input, `Drain()`, then `WaitForRequests` (up to 8 ms, woken by `Post`). The window stays alive and `quit` still works |

While a step runs it is the barrier, so a pipelined `step` + `get_state` gives the state after the frames. `quit` bypasses the barrier and ends even a step of `kMaxStepFrames`. A call that defers frames for another reason (`screenshot` asks for one frame of `dt` 0) uses the same machinery: `IsStepDue()` is true while any deferred call is pending, so the next frame runs and its `EndFrame()` counts it down. All of this state lives in `ControlService`, so `ControlServiceTest` plays the application loop (`RunDueFrames`) without an application.

What the manual clock means for a client:

- Game time and the frame number stand still between steps. A request that changes the state (`perform_action`, `reset`) applies at once, in the idle cycle, but the scene reacts (logic, render, the `state_changed` push) in the next frame, so send a `step` after it.
- Tasks, animations and fixed updates also move only with steps; `dt` 0 makes frames in which nothing moves.
- The window is not redrawn between steps. A frame renders as usual, including the present.
- `--manual-clock` without `--control` is an error: nobody could step the game.

## Starting the API

The API is off by default: a game has to be asked for it. `Application::Initialize()` loads the reflected `ApplicationConfig` (`LoadApplicationConfig()`) and calls `ResolveControlSettings(args, load)` right after the core services are up (before the window exists) and, when the API is enabled, starts the `RemoteControl` server after `GameManager::Initialize()`, so the game methods are registered before the first client connects.

| Command line | `ApplicationConfig.xml` root attribute | Meaning |
|--------------|----------------------------------------|---------|
| `--control` | `control="true"` (or `false`, `1`, `0`) | start the WebSocket server |
| `--control-port=N` | `controlPort="N"` | port 0 to 65535; 0 asks the system for a free one; default `kDefaultControlPort` (8765) |
| `--headless` | `headless="true"` (or `false`, `1`, `0`) | no window: the SDL software renderer on the dummy video driver, a screenshot is the way to see the game (`SDL/CLAUDE.md`, "Headless and screenshots"); needs the API and the Game run mode |
| `--manual-clock` | `manualClock="true"` (or `false`, `1`, `0`) | frames run only on `step` (see "Manual clock and `step`"); needs the API. The flag only turns it on, so a game that has `manualClock="true"` in its config cannot switch it off from the command line |

The file half is the reflected `ApplicationConfig` read with strict values (`IDeserializer::SetStrictValues`, `reflection-module.md`): an attribute that is not a boolean or not a number of the field's type is an error of the load under the attribute's name, and the error of one of the four control attributes stops the start-up as `ApplicationConfig: controlPort: Invalid integer value 'abc'` (also when the API is off or a flag would override it). Errors of the other fields (`mode`, `type`) are not control errors: `Application::Initialize()` logs them once (`Config 'ApplicationConfig': mode: Invalid enum value: 'x'`) after the settings were resolved. A port or a boolean in the file may have spaces around it (`controlPort=" 9100 "`, `headless=" true "` is true) and a port a leading `+` (strict values trim and accept both); the flag `--control-port=N` is stricter. The flags are still parsed by hand over the struct (a later stage reads them through a name to value source). The command line beats the attribute, the attribute beats the default. Unknown arguments are ignored (a game may have flags of its own). A value that cannot be read, a bare `--control-port`, and `--control-port=N`, the manual clock or headless mode while the API stays off are errors: the start-up stops with a line in the log (`Application::Initialize()` flushes the log itself, because nothing else does before the first frame), so the person who asked for the API is not left waiting for it. A port that is taken stops the start-up the same way. The settings are in `ControlSettings.h`; a stage that adds a setting adds its field and its two spellings there. What headless means for the window and the renderer reaches the SDL module as `ApplicationOptions` (`ApplicationFabric::Create(config, options)`), not as ControlSettings, which the SDL module does not see.

## Methods

| Where | Methods |
|-------|---------|
| `BECore/Control/ControlBuiltins` | `ping`, `status`, `step`, `quit` |
| `SDL/CoreSDL/SDLScreenshot` | `screenshot` (`{maxSize}` to `{width, height, png_base64}`; `capture_failed` where the renderer cannot capture; `SDL/CLAUDE.md`) |
| `BECore/UI/UIControl` | `ui_tree` (the canvases and elements of the active scene with rects and the validator's findings), `ui_click` (`{name, canvas?, at?}`: a real mouse click on a visible button, toggle or slider, `at` 0..1 the horizontal position, which also sets a slider; the answer has its `action` and `value`; codes `no_scene`, `not_found`, `not_clickable`, `invalid_params`; `UI/CLAUDE.md`, "Control API") |
| `Games/GameControl` | `list_games`, `switch_game`, `reset`, `get_state`, `list_actions`, `perform_action`, `set_state` (`Games/CLAUDE.md`) |

## Adding a method

1. Write a handler `void(ControlCall&)` in the module that owns the behaviour (BECore only for things that need nothing above it).
2. Read `call.Params()` with the checked helpers; answer with `call.Reply(Json)` or `call.Fail(code, message)`. For an answer that needs frames to pass (step, screenshot) use `call.DeferFrames(...)`.
3. Register it with `CoreManager::GetControlService().RegisterHandler("name", handler)` where the owning module starts up (registering again replaces the handler).
4. Test it with a `ControlService` of its own and a lambda as the reply function (`tests/BECore/ControlServiceTest.cpp` shows the pattern): no transport needed.
5. Write the method and its codes into the protocol description of the owning module.

## Tests

`tests/BECore/ControlServiceTest.cpp` (queue, order across threads, barrier, deferred answers, hooks, the manual clock and `step`, shutdown, builtins), `GameControlTest.cpp` (the state-change push), `JsonTest.cpp` (checked helpers, garbage, nesting), `Base64Test.cpp` (RFC 4648 vectors), `ControlSettingsTest.cpp` (priority of the flags, attributes and defaults, the manual clock, every kind of invalid value). The transport has its own tests, see `RemoteControl/CLAUDE.md`.
