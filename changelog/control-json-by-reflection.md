# Control API, rooms and ArtGen answers written by the generated WriteJson from reflected structs

- Version: 0.12.0
- Entry: control-json-by-reflection
- Date: 2026-11-23
- Bump: minor

## Added
- Reflected answer structs for the control API: `ControlClock` (with `Read()`), `ControlWindowStatus`, the answers of `ControlBuiltinAnswers.h`, the profiler, audio, clipboard, UI, ArtGen and schema answers (`Control/SchemaAnswers.h`), and `ControlCall::ReplyObject(value)`, which writes a `BE_CLASS` through the generated `WriteJson`. An answer that carries the clock derives `ControlClock`; the generator merges the parent's keys, sorted.
- The generated `AcceptPropertyVisitor` names its parameter so that a reflected field may be called `v`.

## Changed
- Every plain answer of the control API, every room message and the editor's ArtGen answers are written by the generated writer instead of member by member; the DOM twins are gone. What stays by hand (the profiler's frames, `explain_value`, `mismatch`, the state of `run_until`, the payloads that hold a `Json`, `DemoCardGame`, the five room messages with a payload) says why in a comment. Room messages are byte for byte the same.

## Breaking
- `WriteControlClock`, `WriteControlClockWith`, `DescribeControlClock` and `WriteWindowStatus` are removed (the callback form cannot be written on the new path). Adapt: an answer that carries the clock is a `BE_CLASS` deriving `ControlClock`, filled by `ControlClock::Read()`, and the control method replies with `call.ReplyObject(answer)`; the window is `ControlWindowStatus::Read()`.
- The `Json` builders of the schema export (`TypeDescJson`, `TypeEntryJson`, `DescribeTypeJson`, `ListTypesJson`, `BuildSchemaJson`) return reflected structs now (`MakeTypeSchemaDesc`, `MakeTypeSchemaEntry`, `DescribeTypeSchema`, `MakeListTypesAnswer`, `MakeSchemaAnswer`). Adapt: a caller writes the answer with `ReplyObject` or `JsonWriter`.
- Protocol: a member that was `null` is absent: `list_games.active`, `audio_state.backend`, `audio_state.music.clip`, the `artgen_*` answers' `usedOn`, `atlas`, `atlasGroup`, `run.seed`, `run.reference`, `sets[].reference`, `sets[].atlas`, and `parent` of a root type in `list_types`, `describe_type` and `schema`. Adapt: a game's QA script or tool that tests for `null` tests for a missing key; one that reads with a default (`.get(key)`) needs nothing.
- Protocol: `ui_tree` canvases give `regions` as an array of `{name, rect}` sorted by name instead of an object keyed by name, a slider element's number is `sliderValue`, and an element at the depth cap (100 levels) with children has `truncated: true`. Adapt: a game's QA script or invariants module reads the region entry with that `name` and a slider's `sliderValue`.
- Protocol: a method row of the schema export has `isConst` instead of `const`; `kSchemaVersion` stays 1. Adapt: a game tool that reads `const` of a method reads `isConst`.
