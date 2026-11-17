---
name: cpp-senior-programmer
description: A map for writing C++ in BagiEngine and in a game on it - which nested CLAUDE.md or rule to read for a task, a real file to copy the idiom from, the traps, a self-check of a diff and the steps that end a plan. It restates nothing of CLAUDE.md. Use when writing or reviewing C++ code here, designing an API, planning a C++ change, or asking how the engine does something in C++.
---

# C++ in BagiEngine

The rules are `CLAUDE.md` ("Code style": DO, DON'T, the comment rule, zero allocations in a steady frame) and the nested `CLAUDE.md` of the folder you touch; this skill does not repeat them. It says where to read and which file to copy. The idioms with their files: [patterns.md](patterns.md).

## Before the first line

1. Read `CLAUDE.md` "Code style" and the nested `CLAUDE.md` of every folder the change touches.
2. Find two or three neighbours of the same kind (a component, a loader, a widget, a control method) and copy their shape: names, includes, the order of the class.
3. Take the row of the task below.

## Task -> what to read -> a file to copy

Paths are from the engine root (`engine/` in a game).

| Task | Read | Copy from |
|------|------|-----------|
| Types, containers, strings as keys | `src/Modules/BECore/Types/CLAUDE.md` | any `BECore` header; `Vector`, `HashMap`, `String`, `UniquePtr` |
| An enum | `CLAUDE.md` DO (`CORE_ENUM`) | `src/Modules/BECore/Save/SaveGame.h` (`SaveErrorKind`), `src/Modules/BECore/Utils/EnumUtils.h` |
| A callback (stored, a parameter, never allocating) | `src/Modules/BECore/Utils/CLAUDE.md` "The callable types" | `src/Modules/BECore/Save/SaveGame.h` (`FunctionRef`), `src/Modules/BECore/Animation/Tween.h` (`FixedFunction`) |
| An event, a subscription | `src/Modules/Events/CLAUDE.md` | `src/Application/Application.cpp` (`Subscribe`), `src/Modules/BECore/Scene/Components/ClickableComponent.cpp` (`SubscribeInScene`) |
| A component | `src/Modules/BECore/Scene/CLAUDE.md` "Adding a component", "Gotchas" | `src/Modules/BECore/Scene/Components/ClickableComponent.cpp` |
| Reflection (`BE_CLASS`, `BE_REFLECT_FIELD`, `BE_EVENT`) | `.claude/rules/reflection-module.md`, `src/Modules/BECore/Reflection/CLAUDE.md` | the component above |
| A background job, a coroutine | `src/Modules/TaskSystem/CLAUDE.md` "Best Practices" | `src/Modules/BECore/Text/FontLoader.cpp` (`Task<>`, `co_await SwitchToBackground`) |
| Locks and threads | `src/Modules/BECore/Threading/CLAUDE.md` | `src/Modules/BECore/PoolString/PoolString.cpp` (`SharedMutex` + `BE_LOCK_SITE`) |
| Text out of numbers | `src/Modules/BECore/Format/CLAUDE.md` | `src/Widgets/EditorLayout/StatusBar.cpp` (a fixed buffer, formatted on a change) |
| A recoverable error | `CLAUDE.md` DO (`std::expected`) | `src/Modules/BECore/Save/SaveGame.cpp` (`Load`) |
| Access control, a global service | `CLAUDE.md` DO (`PassKey`, `Singleton`) | `src/Modules/BECore/Time/Time.h` (`Tick(PassKey<Application>)`), `src/Modules/Games/src/Games/GameManager.h` |
| Steady-frame allocations, zones | `src/Modules/BECore/Profiler/CLAUDE.md` | `tests/BECore/EditorBarsTest.cpp` (`AllocTracker::GetThreadCounters()`) |
| UI (canvases, controls, layout) | `src/Modules/BECore/UI/CLAUDE.md` "Where to read", `.claude/rules/ui-*.md` | the files that doc names |
| An editor panel | `.claude/rules/widgets-module.md` | `src/Widgets/Console/ConsoleWidget.cpp` |
| Saves | `src/Modules/BECore/Save/CLAUDE.md` | `src/Modules/BECore/Save/SaveGame.cpp` |
| A control API method | `src/Modules/BECore/Control/CLAUDE.md`, `.claude/rules/ui-control-api.md` | the methods that doc names |
| Tests | `.claude/rules/tests-module.md` | a test of the same module in `tests/` |

## Traps

Each is written where it lives; these are the ones a change outside that folder walks into.

- `MyEvent::Subscribe<...>(this)` does not compile: subscribe through `SubscriptionHolder` (`Events/CLAUDE.md`). A component subscribes with `SubscribeInScene`, or it reacts while its scene is inactive (`Scene/CLAUDE.md` "Gotchas").
- `"x"_intern` / `PoolString::Intern` on text from outside (a client, a file, input) grows a table that never frees: look it up with `PoolString::Find` (`Reflection/CLAUDE.md`).
- `Format(...)` and `_format` make a `String`, so one per frame breaks the steady frame: format into a member buffer (`FormatToN`, `Format/CLAUDE.md`) and only when the value changed.
- `MoveOnlyFunction` allocates once its capture is above the inline buffer; a callable that must never allocate is a `FixedFunction`, which rejects what does not fit (`Utils/CLAUDE.md`). A `FunctionRef` is never stored.
- `ASSERT` / `EXPECT` compile out in Release: nothing with a side effect inside.
- `co_await` with a lock held, `Wait()` on the main thread, a background loop without `YieldIfNeeded()`: all hang one build or another (`TaskSystem/CLAUDE.md` "DON'T").
- A new file or a new `BE_CLASS` / `BE_REFLECT_FIELD` needs `cmake --preset` again; a header whose `.gen.hpp` no `.cpp` includes fails to link (`reflection-module.md`).
- A change to what a content file may say adds a migration step (`CLAUDE.md` "After making changes").

## Self-check of a diff

- [ ] Every name it uses exists (grep); nothing is copied from memory of another engine
- [ ] `BECore` aliases, `CORE_ENUM`, the callables, `Mutex` + `BE_LOCK_SITE`, `Format`: the CI checks (`BagiTypesCheck`, `BagiIncludeStyle`, `BagiHeadersStandalone`) pass
- [ ] Subscriptions go through `SubscriptionHolder`; an order that matters is an explicit call, not an event
- [ ] An operation that fails changes nothing; a choice that changes nothing does nothing
- [ ] A steady frame allocates nothing: derived things rebuild on a moved key, lists are members, no per-frame `Format`
- [ ] Comments follow the comment rule; no abstraction the task does not need
- [ ] A test of the behaviour, safe beside others (`tests-module.md`)

## The end of a plan

A plan with more than a few lines of C++ ends with these tasks, after the code:

1. **Format:** the `clang-format` skill (changed files, or all for a broad change).
2. **PCH includes:** the `strip-pch-includes` skill after include changes (`.cpp` only; by subfolder with a dry run first when the change is broad).
