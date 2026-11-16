---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/BECore/Logger/**/*"
  - "tools/control/**/*"
  - "tests/BECore/Control*"
---

# The QA methods of the control API (`log_*`, `batch`, `run_until`)

Additive to protocol version 1. The module: `src/Modules/BECore/Control/CLAUDE.md`; the Python side: `tools/control/CLAUDE.md`. Every answer below is the `result` of an ordinary response; every method has a `DescribeMethod` (`list_methods`).

## The log

`ControlLog` (owned by `ControlService`, `service.Log()`, started by `RegisterControlBuiltins`) listens to `LogEvent` on the main thread, so it sees every line whatever the `minLevel` of the sinks. It counts the totals of Warning (`warnings`) and Error plus Fatal (`errors`) since the start and keeps the last `kLogCapacity` (1024) lines of Warning and above in a `LogHistory`. A line has a `sequence` counted from 1 over the kept lines only; a **mark** is the next sequence.

| Method | Params | Answer |
|--------|--------|--------|
| `status` | none | also `log: {warnings, errors}` (totals since the start) |
| `log_mark` | none | `{mark, warnings, errors}` |
| `log_since` | `{mark, level?}` | `{lines: [{sequence, level, time, message, category?}], warnings, errors, dropped, next, truncated?}` |
| `log_subscribe` | `{enabled, level?}` | `{enabled, level}` |

- `level` is `"warning"` (default: Warning, Error, Fatal) or `"error"` (Error, Fatal); anything else, and a `mark` that is missing, not an integer or negative, is `invalid_params`. The `level` of a line is `warning`, `error` or `fatal`. 0 and 1 both mean "since the start".
- `lines` are oldest first, at most `kMaxLogSinceLines` (200, the newest; `truncated: true` when cut). `warnings` / `errors` count the **kept** lines since the mark by level, whatever the filter and the cap. `dropped` is how many lines since the mark the ring has already lost: their level is unknown, so with `dropped > 0` the two counts are **lower bounds**. `next` is the mark for the next check.
- **Freshness:** `status`, `log_mark` and `log_since` call `LogEvent::Flush()` first, so a line logged by a handler or a worker thread earlier in the same frame is counted without a step.
- **The push:** `log_subscribe` is for the whole service (every client gets the events; off by default, so no existing session changes). While on, a frame end hook publishes `{"event": "log", "data": {sequence, level, time, message, category?, skipped?}}` per new line at or above `level`, at most `kMaxLogPushPerFrame` (16) per frame; the lines over the cap are not pushed (`log_since` has them) and `skipped` on the last one pushed counts them. Turning it on starts at the next line, with no replay.

## `batch`

`{calls: [{method, params?}], continueOnError?: false}` to `{results: [{ok: true, result} | {ok: false, error: {code, message}}], stopped?: index}`.

- `calls` is 1 to `kMaxBatchCalls` (1000) objects, each with a non-empty string `method` and an object `params` (default `{}`); a bad shape is `invalid_params` and nothing runs. The request itself is `ok: true` whenever its own params are valid.
- The calls run in order on the main thread. An error answer ends the batch (its entry is the last of `results`, `stopped` is its index) unless `continueOnError`. `batch` inside a batch and `quit` are refused per call (`invalid_params` in that call's entry); an unknown method is that call's `unknown_method`.
- A call that waits for frames (`step`, `run_until`, `screenshot`, `clipboard_*`) makes the batch wait: the calls behind it run when its frames have passed, and the requests behind the batch wait as for any deferred call (the batch is one barrier). The answer is the sum of its calls, so a batch of screenshots is big; the 1 MiB limit is for requests, not answers.

## `run_until`

`{condition, maxFrames?, dt?}` to `{met, frames, reason?, game?, version?, <the clock of step>}`; registered by `RegisterGameControl` (it reads the state of the active game). `condition` has exactly one of:

- `{"pointer": "/state/score", "equals": <json>}`: the RFC 6901 pointer into what `get_state` answers (`{game, version, state}`; `""` is the whole document) equals the value (JSON equality: `true` is not `1`). A path that leads nowhere is "not met", not an error; a pointer that is not syntactically a pointer is `invalid_params`.
- `{"versionMoved": true}`: the state version of the active game differs from the one when the call began.
- `{"event": {"name": "state_changed", "equal"?: {member: value}}}`: a push of that name whose `data` has these members arrives after the call began (also with no event sink).

`maxFrames` is 1 to `kMaxStepFrames` (default 600), `dt` as in `step`. The condition is checked once before any frame (`frames: 0`, `met: true`) and at the end of every frame, after the frame end hooks, so the `state_changed` of that frame counts. **Reaching the limit is not an error**: `{met: false, frames: maxFrames}`, the client decides. `reason` is `pointer`, `versionMoved` or `event` when met. `pointer` and `versionMoved` need an active controllable game (`no_active_game`, `not_controllable`); `event` needs none (`game` and `version` are then left out). A bad condition (none or several keys, a bad pointer, a bad `maxFrames`) is `invalid_params`.

## Python and the QA skills

`GameSession.log_counts()`, `log_mark()`, `log_since(mark, level)`, `log_subscribe(enabled, level)`, `batch(calls, continue_on_error)` (answers `{results, stopped?}`), `run_until(condition, max_frames, dt)` with the condition builders `pointer_equals`, `version_moved`, `event_occurs`; the MCP tools have the same names (`run_until` takes `pointer` + `equals`, `version_moved` or `event` + `event_equal`, exactly one). A call that waits gets a timeout of its frames (the `step`s of a batch summed, `maxFrames` of a `run_until`). A recording holds a `batch` as one line.

For a run of a QA session: `log_mark` before, act in one `batch`, wait with `run_until` instead of polling `get_state` with `step`, `log_since` after (a clean run is `errors: 0, dropped: 0`).

"No new error" as a scenario line (`scenario.py` already has the words; mark 0 is the start of the game, which a replay always begins):

```json
{"method": "log_since", "params": {"mark": 0, "level": "error"}, "expect": {"equal": {"errors": 0, "dropped": 0}}}
```

## The game's recording and the scenario recording

`record_start`, `record_stop` and `replay` (`Games/CLAUDE.md`, "Record and replay") record the **game's** accepted calls in the engine, whoever made them (a client, a person's click), and play them back headless or in a window on the manual clock, checking the outcome of every action; a game has to opt in (`IsReplayable`). The Python recording above is a **client's** session with the expectations the client wrote, replayed by `bagi_play.py` through the API against any game. The two files are not interchangeable. `record_start` and `replay` take any path of the host (a control client can overwrite or read any file the process can), as the loopback API trusts the person at the machine.
