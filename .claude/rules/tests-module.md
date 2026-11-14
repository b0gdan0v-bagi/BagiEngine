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
| GoogleTest (unit / module) | `tests/` (target `BagiEngineTests`) and `tools/stress/tests/` (`StressTests`); a game's tests live in its repository | `ctest --preset <preset>`, or the binary directly | Everything that can run without the game loop: containers, reflection, serialization, events, tasks, file system layering, game rules |
| `ITest` (runtime) | `src/Modules/BECore/Tests/`, listed in `config/TestsConfig.xml` | At game startup, from `CoreManager::OnApplicationPreInit()` | Checks that need the live game engine (window, renderer, scene) |

Default to GoogleTest. Add an `ITest` only when the check cannot run in the test executable.

## GoogleTest (`tests/`)

```
tests/
├── CMakeLists.txt      # target BagiEngineTests via be_add_game_tests
├── Common/             # BagiEngineTestSupport (static lib): TestMain.cpp, EngineEnvironment, AssertBridge, Printers.h, RecordingRenderer.h
└── BECore/             # engine tests (+ ReflectionTest.h / ReflectionEventTest.h / SceneEventsTest.h / TimeTest.h: reflected test data)
    └── Rooms/          # the Rooms module and Games' ISeatedGame: FakeSeatedGame, HubHarness (TestClient.h), RoomsScenarioTest.cpp, and the socket tests over `RoomServer`: RoomSocketClient.h, WireJudge.h, ManualClock.h, RoomServerScenarioTest.cpp, RoomHostileTest.cpp, RoomServerHostileTest.cpp, RoomTableDriver.h (src/Modules/Rooms/CLAUDE.md and src/Modules/RoomServer/CLAUDE.md, "Tests"), and the client: FakeRoomTransport.h, HubTransport.h, JudgedTransport.h, RoomClientTest.cpp, RoomClientHubTest.cpp, RoomClientSocketTest.cpp (src/Modules/RoomClient/CLAUDE.md)
tools/stress/tests/     # StressTests (be_add_game_tests in tools/stress/CMakeLists.txt); a game's tests are in its repository: tests/, built as <Name>Tests
```

```bash
cmake --build --preset macos-debug --target BagiEngineTests StressTests
ctest --preset macos-debug                                       # one process per test, 8 at a time
ctest --preset macos-debug -j1                                   # one at a time
build/macos-debug/bin/BagiEngineTests --gtest_filter='Reflection*'    # cwd must be inside the repository (config/ is found by walking up)
```

### Adding a test

1. Add `tests/<Module>/<Name>Test.cpp` for the engine, or `tests/<Name>Test.cpp` of a game's repository (the CMake globs pick it up, so re-run `cmake --preset ...`).
2. Write `TEST(<Name>Test, <Case>)` inside `namespace BECore::Tests`. One case per behavior, not one big `Run()`.
3. Compile-time checks are `static_assert` at file scope, plus an empty `TEST(<Name>Test, CompileTimeChecks)` so they show up in the report.
4. Include `<Common/Printers.h>` when comparing EASTL strings or `PoolString` with `EXPECT_EQ`; it makes failures readable.
5. Test data with `BE_CLASS` / `BE_EVENT` goes into a header next to the test (see `tests/BECore/ReflectionTest.h`). For engine tests its directory must be in `source_dirs` and `scan_dirs` of `CI/meta_generator/reflection_targets.json`; include the `.gen.hpp` from the `.cpp` only. Reflected test data in a game's tests would need `SOURCE_DIRS` on that game's `be_add_game_reflection` call.

### Rules

- Use gtest macros (`EXPECT_*`, `ASSERT_*`), **not** the engine `ASSERT` / `EXPECT`: the engine ones are compiled out in Release presets, which would make a test pass without checking anything. `ASSERT_*` where the next line depends on the check, `EXPECT_*` otherwise. In a helper that must stop the test, use `ASSERT_*` and call it through `ASSERT_NO_FATAL_FAILURE(...)`.
- A failed engine `ASSERT` / `EXPECT` / `FATALERROR` inside the tested code is reported as a gtest failure by `AssertBridge` (Debug presets only, because the engine macros are off in Release).
- Tests that need the engine services (FileSystem, ConfigManager, ResourceManager, TaskManager) call `EngineEnvironment::EnsureCoreServices()` from `SetUpTestSuite()`. Pure-logic tests must not, so they do not read `config/` or write `engine.log`. The environment pins the engine's `config/` and `assets/` (`BAGI_ENGINE_ROOT`); a game's own content is not mounted.
- A test that needs an `IRenderer` and no window or GPU uses `RecordingRenderer` (`tests/Common/RecordingRenderer.h`, header only): it records every `DrawFilledRect` / `DrawTexture` / `DrawGeometry` (`DrawKind::Geometry`, the vertices in output pixels) in output pixels with the tint and the clip in effect, reports a settable `width` x `height`, and `CreateTexture()` returns a `RecordedTexture` that keeps its pixels. Set it with `CoreManager::GetRendererManager().SetRenderer(...)` and reset it in `TearDown` (`tests/BECore/RendererPrimitivesTest.cpp`, `SceneViewTest.cpp`). The renderer loads no texture from a path, so a test that needs a `SpriteRendererComponent` to draw gives it one: `sprite->SetTexture(renderer->CreateTexture(...))` (`SpriteRendererComponentTest.cpp`). A check of the real SDL pixels uses the headless pair of `SDLCaptureTest.cpp`.
- A test of a UI screen runs its canvas over the size matrix: `ValidateOnMatrix(canvas)` (`tests/Common/UILayoutMatrix.h`, header only) lays a `UICanvasLayout` out for ten aspect ratios at 480 to 2160 px short sides with no insets, a notch and side insets, each scale mode and user scales 1.0 and 1.5, and returns the cases the validator found issues in. `EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str()`. It needs no scene, window or renderer (`src/Modules/BECore/UI/CLAUDE.md`). A test of `UICanvasComponent` itself loads scenes from XML into a `RecordingRenderer` (`tests/BECore/UICanvasComponentTest.cpp`); input goes through `CoreManager::GetUIInput().Dispatch*()`, and `TearDown` resets the scene manager so the canvases unregister.
- A test of a component that switches scenes (`SceneManager::SetActiveScene` through `CoreManager::GetSceneManager()`, which is empty in the gtest executable) hands the manager it built to `EngineEnvironment::UseSceneManager(manager)` after `Initialize()` and takes it back with `UseSceneManager({})` in `TearDown`, before releasing it. A test that loads the **game's own** scene file reads it from the source tree (`std::filesystem::path(__FILE__)`), since a game's content is not mounted, and its `be_add_game_tests` call links the game module with `WHOLE_ARCHIVE` so that the components named in the file are registered.
- **A test runs beside others.** `ctest --preset <p>` runs 8 tests at once (`execution.jobs` of `test-base`), each case in its own process, and the parallel todo-loop workers run whole `ctest`s at the same moment. A test may share read-only inputs (`config/`, `assets/`, the source tree) and nothing it writes or binds. A test that writes files gets the folder or file name from `tests/Common/TempPath.h`: `TestTempDirectory(prefix)` (`<prefix>_<test name>_<8 hex digits drawn once per process>`, for the folder the constructor clears; `UIDocumentTest`, `UIWebTest`, `FileSystemTest`) or `TestTempPath(stem)` (a file in the system temp folder; `ReflectionTest`). A name made of the test name alone is safe inside one `ctest` run but not between two. A test does not read the `engine.log` of the working directory: every process that calls `EnsureCoreServices()` truncates it (a test that checks a log points its sinks at a folder of its own, `ConfigEditorSaveTest`). A limit that can only fail on a loaded machine is a poll with a generous time limit (`WaitUntil` 20 s, `kWaitLimit` 30 s: a passing test never waits it out), not a fixed `sleep_for` followed by a check; a pause before a negative check ("nothing has happened yet") is fine, since load only weakens it.
- `RUN_SERIAL` and `RESOURCE_LOCK` are for what cannot be made safe by a name or a port. `RESOURCE_LOCK <name>` keeps the tests with the same lock apart and lets everything else run (`BagiStressControlSmoke`: `bagi_control_smoke`); `RUN_SERIAL TRUE` stops the whole run while the test runs, so it is the last resort (a test that needs all the cores, or a machine-wide resource like a fixed port). Both act only inside one ctest run: two checkouts running ctest at once can still clash on a machine-wide resource such as a fixed port. Set them in `set_tests_properties` in the CMakeLists that registers the test. None of the engine's gtest cases needs either; a new one that does gets a line with the reason.
- Do not start several `ctest`s in one build folder at the same moment right after a build: `gtest_discover_tests(... PRE_TEST)` rewrites `<Exe>[1]_tests.cmake` when the executable is newer, and two ctests that start together write it at once and leave it corrupt (`Parse error ... StressTests`). The repair: delete `build/<p>/**/<Exe>*_tests.cmake` and run `ctest -N` once. One `ctest -j8` is not affected; after the first `ctest` (or `ctest -N`) on the new build several can run.
- The tests of the RmlUi host (`tests/RmlUiHost/`) exist only in a build with `-DENGINE_UI_RMLUI=ON`; `src/Modules/RmlUiHost/CLAUDE.md` says how to build and run them without disturbing the default build directory.
- A test of the content files of a folder (`config/`, a game's too) calls `ExpectContentFilesAtCurrentVersion(folder)` (`tests/Common/ContentFilesCheck.h`, header only): one failure per file whose `contentVersion` is not the engine's (`Config/CLAUDE.md`, "Content version"); the folder is read from the source tree through `__FILE__`.
- A test of `FileSystem` itself builds its own instance on temporary directories (see `tests/BECore/FileSystemTest.cpp`) instead of using the global services.
- A test of the control API makes a `ControlService` of its own and passes a lambda as the reply function, so it needs no transport and no engine services (see `tests/BECore/ControlServiceTest.cpp`). Do not read client-shaped JSON with nlohmann's throwing accessors, not even in a test: they abort the whole test process (`BECore/Control/CLAUDE.md`).
- A test of a transport (`tests/BECore/WebSocketServerTest.cpp`, `WebSocketListenerTest.cpp`) starts the server on port 0, talks to it through the raw client of `tests/BECore/WebSocketTestClient.h` (a `Socket` plus hand-written frames; its `WaitPump` is what the waiting test thread does) and plays the main thread itself: it calls `ControlService::Drain()` in the loop that waits for an answer, with a time limit and no fixed `sleep`. The parallel run is safe because every test takes a free port. A test that needs the whole game goes through `be_add_game_control_tests` (`BagiStressControlSmoke`), not into a gtest executable. One that needs only an SDL window and renderer makes a headless pair (`tests/BECore/SDLCaptureTest.cpp`: `SDLMainWindow::SetHeadless(true)`, then `SDLRendererBackend`; it skips itself where SDL has no dummy video driver).
- A test of a Python control tool (`tools/control/tests`) runs against a fake control server and a fake game executable (`fake_control_server.py`, `fake_game.py`), so it needs no built game; a test that does need one skips itself. See `tools/control/CLAUDE.md`.
- A test of the reflection generator (`CI/meta_generator/tests`, pytest, run by hand like the control tools') writes a header into `tmp_path` and parses it with the `parse` fixture of `conftest.py`, or runs `meta_generator.main()` through the `run_cli` fixture (skipped without `jinja2`). A construct the parser cannot handle gets a test that asserts its warning; see `.claude/rules/reflection-module.md`, "Codegen".
- A coroutine that suspends on the thread pool (for example `ResourceManager::LoadAsync`) must be run with `CoreManager::GetTaskManager().Run(...)` and waited on through the returned handle. `Task::GetResult()` resume()s the coroutine in a loop and races with the pool.
- State shared across the process (`PoolString` table, `EventBase<T>` dispatchers, `AbstractFactory<T>`, resource cache) is not reset between tests. Tests must clean up after themselves (unsubscribe, clear the cache). Order dependence is not detected in one process: `ctest` runs each test in its own process, but a plain `BagiEngineTests` run or `--gtest_shuffle` does not.
- Do not wait with a fixed `sleep`; poll a condition with a timeout (see `WaitUntil` in `tests/BECore/TaskSystemTest.cpp`).

## Runtime tests (`ITest`)

Managed by `TestManager`, which fills a `Tests::TestList` from `TestsConfig` with `MakeFromConfig` (an unknown `type` is logged and dropped, `enabled="false"` is skipped); run in `CoreManager::OnApplicationPreInit()` after the core services are up. The reference example is `BECore/Tests/EngineSmokeTest.h`.

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
- [ ] `tests/<Module>/<Name>Test.cpp` (engine) or `tests/<Name>Test.cpp` of a game's repository (game) with `TEST(<Name>Test, ...)` in `namespace BECore::Tests`
- [ ] gtest macros, not engine `ASSERT` / `EXPECT`
- [ ] `static_assert` checks at file scope + `CompileTimeChecks` case
- [ ] Re-ran `cmake --preset ...` and `ctest --preset ...` is green
- [ ] If the test has reflected data: directory in `reflection_targets.json`

## Checklist for New Runtime Test
- [ ] Inherit from `ITest`, add `BE_CLASS(TestName)`
- [ ] `Run()` returns `bool`; failures are reported by returning `false` (and `LOG_ERROR`)
- [ ] `static_assert(ValidTest<TestName>)` after the class
- [ ] Add to `TestsConfig.xml`
