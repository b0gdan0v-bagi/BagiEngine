# Control Module Guidelines

## Overview

The core of the external control API: a program or a model sends commands, the engine runs them on the main thread. This directory has no transport and no game knowledge. A transport (the WebSocket server of the `RemoteControl` module) feeds `ControlService::Post()`; modules above BECore register the methods (`ping`, `status`, `quit` live here; the game methods are registered by `Games`, the screenshot by `SDL`). The goal and the stages are in `TODO.md` ("No external control") and `.claude/plans/`.

| File | Purpose |
|------|---------|
| `ControlService` | Thread-safe FIFO queue of requests, handler table, barrier, deferred answers, push events. One instance: `CoreManager::GetControlService()` |
| `ControlCall` | One request being handled: `Params()`, `Reply()`, `Fail()`, `DeferFrames()` |
| `ControlBuiltins` | `RegisterControlBuiltins()`: `ping`, `status`, `quit` |
| `ControlTypes.h` | `ControlError`, `ControlErrors::k*` (error codes), `ControlReplyFn` |
| `Json.h`, `JsonFwd.h` | nlohmann/json behind checked helpers (see "JSON rules") |
| `Utils/Base64.h` (one level up) | Base64 for the screenshot and the WebSocket handshake |

## Protocol

JSON text, one message per request.

```
request   {"id": 1, "method": "perform_action", "params": {...}}     id: integer or string, required; params: object, optional
response  {"id": 1, "ok": true,  "result": {...}}
          {"id": 1, "ok": false, "error": {"code": "illegal_action", "message": "..."}}
push      {"event": "state_changed", "data": {...}}
```

Error codes are strings and part of the protocol (`ControlErrors`): `parse_error`, `invalid_request`, `invalid_params`, `unknown_method`, `request_too_large`, `internal_error`, `shutting_down`. A method adds its own (`illegal_action`, ...). A request that cannot be parsed gets `"id": null`. `ping` reports `ControlService::kProtocolVersion`; change it when the format changes incompatibly.

## Frame and threads

- `Post()` is the only thread-safe call. It parses the text on the posting thread and queues the request; everything else (`RegisterHandler`, `Drain`, `EndFrame`, `Publish`, `Shutdown`, the handlers, the reply and the event sink) belongs to the main thread.
- `CoreManager::OnGameCycle` calls `Drain()` as stage **1b**: after input, before `TaskManager::Update`. A command therefore sees this frame's input and what it changes is seen by this frame's logic and render. `Application::Run` calls `EndFrame()` after the present.
- Requests run in the order they were posted (per posting thread). Do not use `TaskManager` for commands: its main-thread queue is a priority queue and does not keep the order of equal priorities.
- A handler ends in exactly one of `Reply()`, `Fail()`, `DeferFrames()`; one that does none gets `internal_error` and an error log line.
- `DeferFrames(n, done)` answers later: `done` runs at the end of the frame in which the counter (starting at `n`, at least 1) reaches zero. While a call is deferred it is a **barrier**: the requests behind it wait, so a client that pipelines `step` and `get_state` gets the state after the step. A method registered with `bypassBarrier` (`quit`) still runs, but cannot defer itself.
- `AddFrameEndHook()` runs at every `EndFrame()`, before a deferred call completes (the state-change push uses it).
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

## Methods

| Where | Methods |
|-------|---------|
| `BECore/Control/ControlBuiltins` | `ping`, `status`, `quit` |
| `Games/GameControl` | `list_games`, `switch_game`, `reset`, `get_state`, `list_actions`, `perform_action`, `set_state` (`Games/CLAUDE.md`) |

## Adding a method

1. Write a handler `void(ControlCall&)` in the module that owns the behaviour (BECore only for things that need nothing above it).
2. Read `call.Params()` with the checked helpers; answer with `call.Reply(Json)` or `call.Fail(code, message)`. For an answer that needs frames to pass (step, screenshot) use `call.DeferFrames(...)`.
3. Register it with `CoreManager::GetControlService().RegisterHandler("name", handler)` where the owning module starts up (registering again replaces the handler).
4. Test it with a `ControlService` of its own and a lambda as the reply function (`tests/BECore/ControlServiceTest.cpp` shows the pattern): no transport needed.
5. Write the method and its codes into the protocol description of the owning module.

## Tests

`tests/BECore/ControlServiceTest.cpp` (queue, order across threads, barrier, deferred answers, shutdown, builtins), `JsonTest.cpp` (checked helpers, garbage, nesting), `Base64Test.cpp` (RFC 4648 vectors).
