# Control Module Guidelines

## Overview

The core of the external control API: a program or a model sends commands, the engine runs them on the main thread. This directory has no transport and no game knowledge. A transport (the WebSocket server of the `RemoteControl` module) feeds `ControlService::Post()`; modules above BECore register the methods (`ping`, `status`, `step`, `quit` live here; the game methods are registered by `Games`, the screenshot by `SDL`). The tools that use the API: `tools/control/CLAUDE.md`.

| File | Purpose |
|------|---------|
| `ControlService` | Thread-safe FIFO queue of requests, handler table, barrier, deferred answers, push events. One instance: `CoreManager::GetControlService()` |
| `ControlCall` | One request being handled: `Params()`, `Reply()`, `Fail()`, `DeferFrames()` |
| `ControlLog` | Totals of warnings and errors and the last `kLogCapacity` lines of Warning and above, from `LogEvent` (`service.Log()`; `log_*` methods) |
| `ControlRoots` | The roots of a service: named providers of a `TypedRef` (`game` is the active game) that the by-name methods reach; names held as `String`, never interned; `allowInterning` (default off) lets a write intern client text |
| `ReflectionControl` | `RegisterReflectionControl()`: the by-name methods, called by `RegisterControlBuiltins` |
| `SchemaExport` | The type registry as JSON; pure over a `TypeRegistry`; its rules: `Reflection/CLAUDE.md`, "Schema export" |
| `ControlBuiltins` | `RegisterControlBuiltins()`: `ping`, `status`, `step`, `pause`, `resume`, `list_methods`, `log_mark`, `log_since`, `log_subscribe`, `batch`, `quit` |
| `ControlBuiltinAnswers.h` | The reflected answers of `ControlBuiltins` (`ControlStatusAnswer : ControlClock`, `ControlMethodsAnswer`, `ControlBatchAnswer`, `ReflectionMethodAnswer`, ...); `ControlListedParam` mirrors `ControlParamInfo`, which stays an aggregate (`BE_CLASS` adds a virtual) |
| `ControlClock` | Reflected clock (`frame`, `totalTime`, `deltaTime`, `fixedDeltaTime`, `Read()`); the parent of every answer that carries the clock |
| `ControlSettings` | `ResolveControlSettings(args, ApplicationConfigLoad)`: the start-up flags and the settings of the reflected `ApplicationConfig` (`Application/ApplicationConfig.h`), the manual clock included, a pure function |
| `ControlTypes.h` | `ControlError`, `ControlErrors::k*` (error codes), `ControlReplyFn`, `ControlServerStatus` (listening, port, clients), `ControlParamInfo` / `ControlMethodInfo` (what a module says about a method) and `ControlMethodListing` (what `GetMethods()` returns) |
| `ControlEnvelope.h` | The envelopes as reflected structs: the response (`ControlOkEnvelope`), the error one (`ControlErrorEnvelope`, `ControlErrorAnswer`) and the push (`ControlPushEnvelope`); `id`, `result` and `data` are payloads; a `JsonWriter` failure (`Fail`; later writes are ignored) is the error `ControlCall` and `Publish` answer or drop |
| `JsonWriter` | Streaming JSON writer that appends to a caller's `String`: no DOM, no temporary, no allocation beyond the growth of that `String`; compact or indented, byte-identical to nlohmann's `dump` |
| `JsonPayload` | The reflected leaf of a foreign JSON value in an answer (`Borrowed`, `Streamed`, `Text`, `Owned`; written by `WriteJson`, a `JsonWriter` failure carries a streamed callback's error); the schema says `any` |
| `JsonSerializer` | `ISerializer` into a `Json` (`Take()`); DOM-backed, not the fast path for writing: a reflected value has its generated `WriteJson` (`JsonWriteReflect.h`) |
| `JsonDeserializer` | `IDeserializer` over a `const Json&` the caller keeps alive; always strict, with findings; interim, like the writer (entry `json-deserializer`). The keys asked of a scope sit in an arena the reader owns, rewound when the scope closes; traps in `JsonDeserializer.h` |
| `Json.h`, `JsonFwd.h` | nlohmann/json behind checked helpers (see "JSON rules"); `DumpJson` / `AppendJson` write a `Json` through `JsonWriter` |
| `Utils/Base64.h` (one level up) | Base64 for the screenshot and the WebSocket handshake |

## Protocol

JSON text, one message per request.

```
request   {"id": 1, "method": "perform_action", "params": {...}}     id: integer or string, required; params: object, optional
response  {"id": 1, "ok": true,  "result": {...}}
          {"id": 1, "ok": false, "error": {"code": "illegal_action", "message": "..."}}
push      {"event": "state_changed", "data": {"game": "Stress", "version": 12, "state": {...}}}
```

Error codes are strings and part of the protocol (`ControlErrors`): `parse_error`, `invalid_request`, `invalid_params`, `unknown_method`, `request_too_large`, `internal_error`, `shutting_down`, `manual_clock`. A method adds its own (`illegal_action`, ...); the by-name methods add `unknown_root`, `no_object`, `unknown_type` and the codes of `Reflection/CLAUDE.md`, "Reflection methods". A request that cannot be parsed gets `"id": null`. `ping` reports `ControlService::kProtocolVersion`; change it when the format changes incompatibly.

`state_changed` (`GameControl` publishes it, `Games/CLAUDE.md`) is sent at the end of the frame in which the state version of the active game moved, whoever moved it; `data` is what `get_state` answers (`state` is left out when the game cannot give it). The first frame only records the version. `log` (one per new warning or error line while `log_subscribe` is on) is the other push.

## Frame and threads

- `Post()` is the only thread-safe call. It parses the text on the posting thread and queues the request; everything else (`RegisterHandler`, `Drain`, `EndFrame`, `Publish`, `Shutdown`, the handlers, the reply and the event sink) belongs to the main thread.
- `CoreManager::OnGameCycle` calls `Drain()` as stage **1b**: after input, before `TaskManager::Update`. A command therefore sees this frame's input and what it changes is seen by this frame's logic and render. `Application::RunFrame` calls `EndFrame()` after the present.
- A `Request` and a `ControlCall` are `BE_POOLED` (a request is made on the posting thread and freed on the main one, so the concurrent pool; both show in `frame_stats` `pools`). The posting thread moves `id` and `params` out of the parsed document into the request (no deep copy), and the two queues are `Fifo`s that keep their capacity; `Drain()` walks the inbox once and sends a request held by the barrier to the back, which keeps the held ones in order.
- Requests run in the order they were posted (per posting thread). Do not use `TaskManager` for commands: its main-thread queue is a priority queue and does not keep the order of equal priorities.
- The handler table, the event sink and the hooks hold `MoveOnlyFunction`s. The handler that runs is moved out of its entry for the call and goes back after it unless the call replaced it: a handler may register methods, even its own name, and the replaced handler's capture must outlive the call that is using it. A frame end hook is held the same way.
- A handler ends in exactly one of `Reply()`, `Fail()`, `DeferFrames()`; one that does none gets `internal_error` and an error log line.
- `DeferFrames(n, done)` answers later: `done` runs at the end of the frame in which the counter (starting at `n`, at least 1) reaches zero. `DeferFrames(n, secondsPerFrame, done)` also says how long each frame lasts, which only the manual clock uses (negative or not a number is zero; the default is `kDefaultStepSeconds`, 1/60). While a call is deferred it is a **barrier**: the requests behind it wait, so a pipelined `step` + `get_state` gives the state after the step. A method registered with `bypassBarrier` (`quit`) still runs, but cannot defer itself.
- `SetFrameEndHook(name, hook)` runs `hook` at every `EndFrame()`, before a deferred call completes (the state-change push uses it). Setting a name again replaces its hook, so a module that registers twice (`GameManager::Initialize()` does, once per test) adds nothing. A hook that sets another one is fine: the new one runs in the same frame.
- `Shutdown()` (from `OnApplicationDeinit`) answers everything queued or deferred with `shutting_down`; a request posted later is answered at once on the posting thread. Stop the transport before it.
- Never use `ASSERT` / `EXPECT` / `FATALERROR` on data from a client: a bad request is an error response, not a bug in the engine.
- Method names live in a `HashMap<String, ...>`, not in `PoolString`: a client must not be able to grow the pool (`ControlServiceTest` checks it).

## JSON rules

The engine builds without exceptions, so nlohmann/json is built with `JSON_NOEXCEPTION` and **aborts the whole process** where it would throw. Checked on this build: `at()` on a missing key, `get<T>()` on the wrong type, `value()` on a non-object and `operator[]` by a missing key on a const object all abort.

- Include `BECore/Control/Json.h` in `.cpp` files and in headers that really need the complete type (`ControlCall.h`, a game's adapter). Headers that only mention the type include `JsonFwd.h`. `CoreManager.h` must not pull in the full header (about 900 KB).
- Parse client text with `ParseJson()` only (it uses `allow_exceptions = false`). Read members with `FindJsonMember()`, `GetJsonString/Int/Number/Bool()`, which check the type. `JsonStringView()` reads a string without a copy (valid while the `Json` lives; sized, so an embedded NUL stays). `GetJsonInt` rejects `1.5`, `"3"` and numbers that do not fit `int64_t`; `GetJsonIntValue(value)` is the same rule for a value that is not a member (an array item).
- Never call `at()`, `get<T>()`, `value()` or the const `operator[]` on client data. Building a response (`Json::object()`, `result["x"] = ...`) and `DumpJson()` are safe; `DumpJson()` replaces invalid UTF-8 instead of aborting.
- Keep the 1 MiB request limit (`ControlService::kMaxRequestBytes`) at every transport. `ParseJson` itself takes any depth (it parses and frees without recursion), but copying a value recurses once per level, so a reader that keeps client JSON checks `IsJsonNestedDeeperThan` first: a request nested deeper than `ControlService::kMaxRequestDepth` (128) is `invalid_request`, answered without an `id`. The control API is fuzzed: `tests/Fuzz/CLAUDE.md`.
- **New JSON is written with `JsonWriter`; `Json` is for reading client input and for the few documents that stay a DOM** (the state document of `run_until`, the file merge of `EditorSettings`). `DumpJson` is for a value that already is a `Json`.
- A plain answer is a reflected struct of arena types and `TextView`s, a local of the handler or of `done`, `call.ReplyObject(answer)` (`Reflection/CLAUDE.md`, "Answers on the scratch arena"); an answer with the clock derives `ControlClock`. A foreign JSON value in it (the client's `id`, a game's document) is a `JsonPayload` member, by holder: `Borrowed` for one the handler holds, `Streamed` for a game's document written straight into the writer (a failing callback answers its `ControlError` with `JsonWriter::Fail`), `Text` for ready text, `Owned` only when the struct outlives its source. It has no form outside the JSON wire (no save, no `BE_FUNCTION` result). A hand-written writer stays for the log lines (they stream from the ring), `frame_stats`, `explain_value` and `PropertyValue`.
- Write a message with `JsonWriter` into a `String` that lives as a member (it keeps its capacity, so the second message allocates nothing); never build a `Json` just to dump it (a reflected value: `value.WriteJson(writer)`). A hand-written object writes its keys in **alphabetical order** (`id`, `ok`, `result`): the `Json` DOM sorts them, and clients may rely on that. `Reply` and `Publish` take a callback that writes the value beside the `Json` form; `TryReply` is for a writer that can fail (its error answers, nothing written is sent).
- The `const String&` a `ControlReplyFn` gets is valid for the call only (the service writes every reply into one scratch): a reply function that keeps the text copies it.
- nlohmann uses `std::string` inside; the helpers convert at the edge.

## Manual clock and `step`

`step {"frames": N, "dt": seconds}` is a deferred call: it answers when `N` frames (1 to `kMaxStepFrames`, default 1) have ended, with the clock as `status` reports it (`frame`, `totalTime`, `deltaTime`, `fixedDeltaTime`). `dt` is 0 to `Time::kMaxFrameTime` (default 1/60). Both are client data: out of range, fractional, mistyped or `null` is `invalid_params`.

The two clocks differ in who runs the frames:

| | Real clock (default) | Manual clock (`--manual-clock`) |
|---|---|---|
| Frames | run all the time; `Time::Tick` measures them | run only while the pause has a step due (`GamePause::IsStepDue()`, which `ControlService::IsStepDue()` mirrors); `Time::Advance(step seconds)` sets their length |
| `step` | waits for `N` frames; `dt` is ignored (a game that is **paused** makes `N` stepped frames of `dt`) | makes `N` frames of `dt` seconds, then answers |
| Between steps | | `Application::RunFrame` calls `CoreManager::OnIdleCycle`: input, `Drain()`, then `WaitForRequests` (up to 8 ms, woken by `Post`). The window stays alive and `quit` still works |

While a step runs it is the barrier too. `quit` bypasses the barrier and ends even a step of `kMaxStepFrames`. A call that defers frames for another reason (`screenshot` asks for one frame of `dt` 0) uses the same machinery: `IsStepDue()` is true while any deferred call is pending, so the next frame runs and its `EndFrame()` counts it down.

**The manual clock is a pause** (`CoreManager::GetPause()`, `Time/GamePause.h`; there is no second mechanism): `--manual-clock` starts with the pause set, and `Application::RunFrame` asks the pause for the kind of the frame. With the manual clock, which is always paused, `pause` is a no-op and `resume` fails with `manual_clock`. How a deferred call requests its frames, a `step` on a paused real clock (`N + 1` frames), the `pause` / `resume` answers and what a **held** frame runs: `.claude/rules/control-clock.md`.

What the manual clock means for a client:

- Game time and the frame number stand still between steps. A request that changes the state (`perform_action`, `reset`) applies at once, in the idle cycle, but the scene reacts (logic, render, the `state_changed` push) in the next frame: send a `step` after it.
- The window is not redrawn between steps. A frame renders as usual, including the present.
- Tasks, animations and fixed updates also move only with steps; `dt` 0 makes frames in which nothing moves.
- `--manual-clock` without `--control` is an error: nobody could step the game.

## Starting the API

Off by default (`--control`, `control="true"`); the command line beats the attribute. Flags, config and status: `.claude/rules/control-startup.md`.

## Methods

| Where | Methods |
|-------|---------|
| `BECore/Control/ControlBuiltins` | `ping`, `status` (the clock, `manualClock`, `paused`, `log` totals, `window`), `step`, `pause` / `resume` (`{paused}`; see "Manual clock and `step`"), `list_methods` (to `{methods: [{name, summary, params: [{name, type, required, description}]}]}`, sorted; a method without a description has `""` and `[]`), `log_mark`, `log_since`, `log_subscribe`, `batch` (shapes, limits and traps: `.claude/rules/control-qa-api.md`), `quit` |
| `BECore/Control/ControlWindow` | `set_window` (`{width, height, density?, userScale?, safeArea?}` on a headless window, answered after a frame; `not_headless` on a real one) and the `window` object of `status`; `.claude/rules/control-window-api.md` |
| `SDL/CoreSDL/SDLScreenshot` | `screenshot` (`{maxSize}` to `{width, height, png_base64}`; `capture_failed` where the renderer cannot capture; `SDL/CLAUDE.md`) |
| `BECore/UI/UIControl` | answers are the reflected structs of `UIControlAnswers.h`; `ui_tree` (the canvases and elements of the active scene with rects and the validator's findings), `ui_click` (`{name, canvas?, at?}`: a real mouse click on a visible button, toggle or slider, `at` 0..1 the horizontal position, which also sets a slider; the answer has its `action` and `value` and `takenBy`; codes `no_scene`, `not_found`, `not_clickable`, `invalid_params`; `.claude/rules/ui-control-api.md`) |
| `BECore/Input/InputControl` | `click_at` (`{x, y, space?, button?, which?}`: a click at a point in output pixels or scene units; `Input/CLAUDE.md`) |
| `BECore/Clipboard/ClipboardControl` | `clipboard_set` (`{text}` to `{ok}`), `clipboard_get` (to `{status, text}`; answered after frames, `clipboard_timeout` after 600), `clipboard_paste` (to `{}` once the `PasteEvent` has been delivered, as Cmd+V gives it); `Clipboard/CLAUDE.md` |
| `BECore/Profiler/ProfilerControl` | `frame_stats` (`{frames?}` 1..240: the last frames, oldest first, with stage times, draws, allocations, arena and pool totals, named counters; plus `arenas` and `pools`, one entry each: `Memory/CLAUDE.md`), `profiler_dump` (`{path?}` relative, no `..`: writes the Chrome trace; `not_recording` unless the game runs with `--trace=` or `--profiler=chrome`), `zone_allocs` (`{top?, reset?}`: allocations per zone, per zone x EASTL name (`names`), per zone x size bucket (`sizes`) and the totals; `enabled` false without `ENGINE_PROFILER_ALLOC`); `Profiler/CLAUDE.md` |
| `BECore/Control/ReflectionControl` | `get_property` (`{root, path}`), `set_property` (`{root, path, value}`), `call_method` (`{root, method, arguments?}`) over a root; `list_types` (`{base?, recursive?, creatable?}`), `describe_type` (`{name}`), `schema`; shapes and codes in `Reflection/CLAUDE.md`, "Reflection methods" |
| `BECore/FileSystem/FileWatchControl` | `files_rescan` (to `{changes}`), `files_apply` (`{paths?}` to `{applied}`); codes `not_watching`, `invalid_params`; `FileSystem/CLAUDE.md` |
| `BECore/Audio/AudioControl` | `audio_state` (the backend, the levels, the last sounds asked for, played or not), `audio_set` (`{bus, volume?, muted?}`); `Audio/CLAUDE.md` |
| `BECore/Values/Explain/ValueExplainControl` | `explain_value` (`{root, path, context?}`; `Values/Explain/CLAUDE.md`) |
| `Games/GameControl` | `list_games`, `switch_game`, `reset`, `get_state`, `list_actions`, `perform_action`, `set_state`, `run_until`, `record_start`, `record_stop`, `replay` (`Games/CLAUDE.md`; shape of `run_until`: `.claude/rules/control-qa-api.md`) |

The game methods describe only what `GameControl` itself reads; the parameters of `perform_action` and `set_state` are the game's.

## Adding a method

1. Write a handler `void(ControlCall&)` in the module that owns the behaviour (BECore only for things that need nothing above it).
2. Read `call.Params()` with the checked helpers; answer with `call.Reply(writer)` (a `JsonWriter` callback, keys alphabetical; `Reply(Json)` only for a document you already hold) or `call.Fail(code, message)`. For an answer that needs frames to pass (step, screenshot) use `call.DeferFrames(...)`.
3. Register it with `CoreManager::GetControlService().RegisterHandler("name", handler)` where the owning module starts up (registering again replaces the handler).
4. Describe it with `DescribeMethod("name", {summary, params})` beside the registration, so `list_methods` and the editor's console can offer and complete it. For people only, never checked against the handler; a method without one still runs.
5. Test it with a `ControlService` of its own and a lambda as the reply function: no transport needed.
6. Write the method and its codes into the protocol description of the owning module.

An object a client may read or write by name is offered as a root: `service.Roots().Register("name", {provider})` where the owning module starts up (`RegisterGameControl` registers `game`). The provider is called on every request and its `TypedRef` is not kept past the handler.

## Tests

`tests/BECore/ControlServiceTest.cpp` is the pattern (a service of its own, a lambda as the reply). The transport has its own tests, see `RemoteControl/CLAUDE.md`.
