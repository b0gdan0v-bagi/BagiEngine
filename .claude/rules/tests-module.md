---
paths:
  - "tests/**/*"
  - "src/Modules/BECore/Tests/**/*"
  - "src/Modules/BECore/Assert/**/*"
  - "config/TestsConfig.xml"
  - "config/AssertHandlersConfig.xml"
---

# Test System & Assertions

Two kinds of tests, in two places:

| Kind | Where | Runs | Use for |
|------|-------|------|---------|
| GoogleTest (unit / module) | `tests/` (target `BagiEngineTests`) and `games/<Name>/tests/` (targets `ChessTests`, `Match3Tests`) | `ctest --preset <preset>`, or the binary directly | Everything that can run without the game loop: containers, reflection, serialization, events, tasks, file system layering, game rules |
| `ITest` (runtime) | `src/Modules/BECore/Tests/`, listed in `config/TestsConfig.xml` | At game startup, from `CoreManager::OnApplicationPreInit()` | Checks that need the live game engine (window, renderer, scene) |

Default to GoogleTest. Add an `ITest` only when the check cannot run in the test executable.

## GoogleTest (`tests/`)

```
tests/
├── CMakeLists.txt      # target BagiEngineTests via be_add_game_tests
├── Common/             # BagiEngineTestSupport (static lib): TestMain.cpp, EngineEnvironment, AssertBridge, Printers.h
└── BECore/             # engine tests (+ ReflectionTest.h / ReflectionEventTest.h / SceneEventsTest.h: reflected test data)
games/<Name>/tests/     # a game's tests, built as <Name>Tests by be_add_game_tests in games/<Name>/CMakeLists.txt
```

```bash
cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests
ctest --preset macos-debug                                       # one process per test
build/ClangmacOS/BagiEngineTests --gtest_filter='Reflection*'    # cwd must be inside the repository (config/ is found by walking up)
```

### Adding a test

1. Add `tests/<Module>/<Name>Test.cpp` for the engine, or `games/<Name>/tests/<Name>Test.cpp` for a game (the CMake globs pick it up, so re-run `cmake --preset ...`).
2. Write `TEST(<Name>Test, <Case>)` inside `namespace BECore::Tests`. One case per behavior, not one big `Run()`.
3. Compile-time checks are `static_assert` at file scope, plus an empty `TEST(<Name>Test, CompileTimeChecks)` so they show up in the report.
4. Include `<Common/Printers.h>` when comparing EASTL strings or `PoolString` with `EXPECT_EQ`; it makes failures readable.
5. Test data with `BE_CLASS` / `BE_EVENT` goes into a header next to the test (see `tests/BECore/ReflectionTest.h`). For engine tests its directory must be in `source_dirs` and `scan_dirs` of `CI/meta_generator/reflection_targets.json`; include the `.gen.hpp` from the `.cpp` only. Reflected test data in a game's tests would need `SOURCE_DIRS` on that game's `be_add_game_reflection` call.

### Rules

- Use gtest macros (`EXPECT_*`, `ASSERT_*`), **not** the engine `ASSERT` / `EXPECT`: the engine ones are compiled out in Release presets, which would make a test pass without checking anything. `ASSERT_*` where the next line depends on the check, `EXPECT_*` otherwise. In a helper that must stop the test, use `ASSERT_*` and call it through `ASSERT_NO_FATAL_FAILURE(...)`.
- A failed engine `ASSERT` / `EXPECT` / `FATALERROR` inside the tested code is reported as a gtest failure by `AssertBridge` (Debug presets only, because the engine macros are off in Release).
- Tests that need the engine services (FileSystem, ConfigManager, ResourceManager, TaskManager) call `EngineEnvironment::EnsureCoreServices()` from `SetUpTestSuite()`. Pure-logic tests must not, so they do not read `config/` or write `engine.log`. The environment pins the engine's `config/` and `assets/` (`BAGI_ENGINE_ROOT`); a game's own content is not mounted.
- A test of `FileSystem` itself builds its own instance on temporary directories (see `tests/BECore/FileSystemTest.cpp`) instead of using the global services.
- A coroutine that suspends on the thread pool (for example `ResourceManager::LoadAsync`) must be run with `CoreManager::GetTaskManager().Run(...)` and waited on through the returned handle. `Task::GetResult()` resume()s the coroutine in a loop and races with the pool.
- State shared across the process (`PoolString` table, `EventBase<T>` dispatchers, `AbstractFactory<T>`, resource cache) is not reset between tests. Tests must clean up after themselves (unsubscribe, clear the cache). Order dependence is not detected in one process: `ctest` runs each test in its own process, but a plain `BagiEngineTests` run or `--gtest_shuffle` does not.
- Do not wait with a fixed `sleep`; poll a condition with a timeout (see `WaitUntil` in `tests/BECore/TaskSystemTest.cpp`).

## Runtime tests (`ITest`)

Managed by `TestManager`, run in `CoreManager::OnApplicationPreInit()` after the core services are up. The reference example is `BECore/Tests/EngineSmokeTest.h`.

1. Create a class in `BECore/Tests/` implementing `ITest`
2. Add `BE_CLASS(TestName)` inside the class
3. Implement `bool Run() override` and `eastl::string_view GetName() override`
4. Add `static_assert(ValidTest<TestName>)` after the class
5. Enable it in `config/TestsConfig.xml`: `<test type="TestName" enabled="true" />`
6. Re-run `cmake --preset ...` (registration is generated code)

`ValidTest` checks that `Run()` returns `bool`, that `GetStaticTypeName()` exists (from `BE_CLASS`) and that the class derives from `ITest`.

`ITest` is a `FACTORY_BASE`. The generator registers a base that has no derived class itself, unless the class is abstract (a pure virtual method in its body), so `ITest` needs no concrete descendant for the build to work. Keep `EngineSmokeTest` anyway: it is the startup check.

## Assertion System

**Macros** (from `BECore/Assert/AssertMacros.h`):
- `ASSERT(expr)` / `ASSERT(expr, "msg")` — Critical check
- `EXPECT(expr)` / `EXPECT(expr, "msg")` — Soft expectation  
- `FATALERROR("msg")` — Unrecoverable error

**Key points:**
- Always use `ASSERT` instead of `std::assert`
- Assertions emit `AssertEvent` through event system
- Handlers configured via `config/AssertHandlersConfig.xml`
- Compiled out when `ENGINE_ENABLE_ASSERTS` / `ENGINE_ENABLE_EXPECTS` is 0 (Release presets)

**Built-in handlers:**
- `DebugBreakHandler` — triggers debugger breakpoint
- `LogHandler` — logs assertion failures
- `StackTraceHandler` — prints a stack trace

## Checklist for New gtest Test
- [ ] `tests/<Module>/<Name>Test.cpp` (engine) or `games/<Name>/tests/<Name>Test.cpp` (game) with `TEST(<Name>Test, ...)` in `namespace BECore::Tests`
- [ ] gtest macros, not engine `ASSERT` / `EXPECT`
- [ ] `static_assert` checks at file scope + `CompileTimeChecks` case
- [ ] Re-ran `cmake --preset ...` and `ctest --preset ...` is green
- [ ] If the test has reflected data: directory in `reflection_targets.json`

## Checklist for New Runtime Test
- [ ] Inherit from `ITest`, add `BE_CLASS(TestName)`
- [ ] `Run()` returns `bool`; failures are reported by returning `false` (and `LOG_ERROR`)
- [ ] `static_assert(ValidTest<TestName>)` after the class
- [ ] Add to `TestsConfig.xml`
