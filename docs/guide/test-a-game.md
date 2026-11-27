# Test a game

You will read a test of the demo's scene that runs with no window, run it, break the demo's input example on purpose and watch the test that guards it fail, then look at the other two kinds of test: the smoke of the real program, and a scenario file with a reference picture. Last, how `ctest` runs all of them and what to do when one fails.

You need the build from [Run the demo](run-the-demo.md), and the [control API page](control-api.md) for the scenario part. A game of your own keeps its tests in the `tests/` folder of its repository, with the engine as `engine/` ([NEW_GAME.md](../../NEW_GAME.md)); the demo is a program of the engine repository, so its tests are in `tools/demo/tests/`, and they are the model.

## Three kinds of test

| | What it starts | Takes (this machine) | Good for | Cannot see |
|---|---|---|---|---|
| **A gtest of a scene** | your scene file and components inside the test process, a renderer that only records | under a second for the demo's whole suite | one rule at a time: a click does this, a field ends up that | a window, the real order of a frame, the program's start-up |
| **The control smoke** | the real executable, headless, driven over the control API | about 10 seconds | that the program starts, answers its API, draws and exits clean | your game's rules: it asks what every game answers |
| **A scenario file** | the real executable, replaying recorded calls | about 1 second | the answers to a fixed list of calls, and a picture against a reference | anything the calls do not reach |

A test is for pinning a contract or reproducing a bug. It does not retell the implementation, so it breaks when a rule breaks and not when the code is tidied.

## What the game's CMake says

<!-- source: tools/demo/CMakeLists.txt -->
```cmake
    file(GLOB DEMO_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: the tests that load the shipped SceneConfig.xml name components no test references
    be_add_game_tests(DemoTests
        SOURCES ${DEMO_TEST_SOURCES}
        WHOLE_ARCHIVE DemoModule
    )
    be_add_game_control_tests(BagiDemo)
...
    be_add_game_scenario_tests(BagiDemo DIR tests/scenarios)
```

A game of your own has the same lines with its own names: `<Name>Tests`, `Bagi<Name>`, its module. `WHOLE_ARCHIVE` links the game's module so that every component class is registered: a scene file names components that no test mentions, and a linker would drop them. The sources are globbed, so a new test file needs `cmake --preset macos-debug` once more before it is built.

## A test of a scene, in C++

This is the first case of `DemoControlsTest`, which clicks the demo's panel.

<!-- source: tools/demo/tests/DemoControlsTest.cpp -->
```cpp
    TEST_F(DemoControlsTest, TheAnimateToggleStopsAndStartsTheTweens) {
        LoadScene();
        Update();
        ASSERT_GT(CoreManager::GetTweens().GetActiveCount(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("Animate"));
        Update();
        EXPECT_EQ(Variable(DemoNames::animateVar), DemoNames::off);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
```

`LoadScene` and `Update` come from `DemoSceneFixture`, which every test of the demo's scene derives from. It loads the shipped scene file into a scene manager, with a `RecordingRenderer` as the renderer: a renderer that draws nothing and writes down every call, so a test needs no window or GPU.

<!-- source: tools/demo/tests/DemoSceneFixture.h -->
```cpp
        static constexpr double kFrame = 1.0 / 32.0;  // a power of two: the sums of frame times are exact
...
        /// One frame of the scene: the clock, the tweens, the update (the order of CoreManager::OnGameCycle)
        void Update() {
            CoreManager::GetTime().Advance(kFrame);
            CoreManager::GetTweens().Advance(CoreManager::GetTime().GetDeltaTime());
            _manager->UpdateAll();
        }
```

A test advances the game **by hand**: one call, one frame of exactly one thirty-second of a second. Nothing runs between calls, so the test reads the same numbers on every machine. `Click(name)` draws once so the layout is current, then sends a press and a release to the element as a mouse would, through the same input class the game uses. The price is that a test sees the frame in the order the fixture writes, not the order the real loop would.

Two habits keep a failing test from crashing the run. A helper that must stop the test uses `ASSERT_*` and the case calls it through `ASSERT_NO_FATAL_FAILURE`. A pointer the case reads through goes through `Require(...)` (`tests/Common/Require.h`), which fails the case with a message when the pointer is empty instead of dereferencing it:

<!-- source: tools/demo/tests/DemoGuideActionTest.cpp -->
```cpp
    TEST_F(DemoGuideActionTest, TheExampleHoldsItsPadAndItsEmitterAndNothingBurstsBeforeAClick) {
        const ClickableComponent& pad = BECore::Tests::Require(Node(kPad).GetComponent<ClickableComponent>());
```

Run the suite from the build folder, because a test finds its data by paths relative to it:

<!-- command -->
```bash
(cd build/macos-debug && bin/DemoTests --gtest_filter='DemoControlsTest*')
```

<!-- illustration: the tail of the gtest run on the machine of the writer -->
```text
[----------] 5 tests from DemoControlsTest (139 ms total)

[----------] Global test environment tear-down
[==========] 5 tests from 1 test suite ran. (145 ms total)
[  PASSED  ] 5 tests.
```

### Break it on purpose

`DemoGuideActionTest` loads the example scene of the [input page](input-actions.md), where a click on the pad sends the action `Pop` and the emitter listens for it. Open the example's scene file and change the name the emitter listens for, `Pop`, to `Pip`:

<!-- source: docs/guide/examples/input-actions/config/SceneConfig.xml -->
```xml
            <component type="ParticleEmitterComponent" playOnStart="false" burstAction="Pop">
```

The test reads the scene file from the source tree when it runs, so nothing is rebuilt. Run it:

<!-- command -->
```bash
(cd build/macos-debug && bin/DemoTests --gtest_filter='DemoGuideActionTest*')
```

<!-- illustration: the failing run on the machine of the writer, the lines of the two failed cases -->
```text
[ RUN      ] DemoGuideActionTest.TheExampleHoldsItsPadAndItsEmitterAndNothingBurstsBeforeAClick
tools/demo/tests/DemoGuideActionTest.cpp:57: Failure
Expected equality of these values:
  pad._actionName
    Which is: PoolString("Pop")
  Emitter()._burstAction
    Which is: PoolString("Pip")

[ RUN      ] DemoGuideActionTest.APressOnThePadBurstsTheEmitterNamedByTheSameAction
tools/demo/tests/DemoGuideActionTest.cpp:65: Failure
Expected: (Emitter().GetLiveCount()) > (0u), actual: 0 vs 0
```

Two cases fail and name their lines. The third, a press beside the pad does nothing, still passes, as it should. The first failure says which two names disagree. Put the file back:

<!-- command -->
```bash
git checkout -- docs/guide/examples/input-actions
```

## A component without a scene file

A test of one component needs no scene file and no fixture: `tests/BECore/ClickableComponentTest.cpp` is the model. It builds a few lines of scene XML in a string, loads them into a scene manager on a `RecordingRenderer` and sends the clicks through the same input class. A case that needs the engine's own services (files, config) calls `EngineEnvironment::EnsureCoreServices()` once in `SetUpTestSuite()`; a case of pure logic does not, so it never reads `config/` or touches `engine.log`.

## The smoke of the real program

A gtest runs your classes, not your program. The **control smoke** starts the built executable with no window, drives it over the control API ([the previous page](control-api.md)) and checks what every game answers: that it starts, answers a malformed call and an unknown method with an error and not with silence, steps its clock, takes a screenshot, lists its UI and scene, and exits.

Its line is one word in the demo's CMake file, `be_add_game_control_tests(BagiDemo)`, and the default script is `tools/control/tests/smoke.py`, which knows no game. Run it:

<!-- command -->
```bash
ctest --preset macos-debug -R BagiDemoControlSmoke
```

<!-- illustration: the ctest line on the machine of the writer -->
```text
1/1 Test #72: BagiDemoControlSmoke .............   Passed   11.04 sec
```

A game with rules of its own writes its own script. It imports the helpers of `smoke.py` and calls `run_smoke()` with the scenarios it wants, so it can start a game in each scene and check the rules through the API. `BagiStress` does:

<!-- source: tools/stress/CMakeLists.txt -->
```cmake
    # the control smoke of the app: its own script (a fresh game per scene, the check of the game's log)
    be_add_game_control_tests(BagiStress SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress_smoke.py)
```

## A scenario in the repository

A scenario is a recorded session replayed as a test ([how to record one](control-api.md#a-scenario)). The demo keeps one, and it is what a game copies:

<!-- source: tools/demo/tests/scenarios/title.jsonl -->
```json
{"scenario": 1, "game": "Demo", "flags": ["--headless", "--manual-clock"], "title": "BagiDemo: the title text set by scene_set is the reference picture title.png"}
{"method": "scene_set", "params": {"node": "Text/Title", "component": "TextComponent", "path": "text", "value": "Hello from the demo scenario"}}
{"method": "step", "params": {"frames": 2, "dt": 0.016666666666666666}}
{"method": "screenshot", "params": {"maxSize": 400}, "expect": {"reference": {"png": "references/title.png", "threshold": 24, "maxShare": 0.002}}}
```

It sets the title text, steps two frames and compares the picture with `references/title.png`, next to the file. `threshold` is how far one colour channel may differ before a pixel counts as changed, and `maxShare` is the share of changed pixels allowed, so another machine's anti-aliasing of the letters passes and a changed word does not. The line `be_add_game_scenario_tests(BagiDemo DIR tests/scenarios)` of the CMake excerpt above makes every `*.jsonl` of the folder an entry, `BagiDemoScenario.title` here; a new file needs no CMake edit.

Play it by hand with the player, outside ctest:

<!-- command -->
```bash
python3 tools/control/bagi_play.py build/macos-debug/bin/BagiDemo tools/demo/tests/scenarios/title.jsonl
```

<!-- illustration: the player's answer on the machine of the writer -->
```text
PASSED tools/demo/tests/scenarios/title.jsonl: 3 calls
```

A failing scenario stops at the first line that does not hold, prints what it expected and what it got, and, for a picture, writes the actual picture and a difference map into a folder outside the repository and names both; [the control page](control-api.md#a-scenario) shows the whole output. If the picture changed **on purpose** (you changed the title's font), play the file again with `--update`, which retakes the picture, then look at the new PNG and review it like code: it is a file of the repository. `--update` for a failure you do not understand only hides the bug. A scenario that fails can be cut to the calls it needs with `--shrink`.

## Running them

<!-- command -->
```bash
ctest --preset macos-debug
```

This builds nothing; build first (`cmake --build --preset macos-debug`). It runs the gtest executables in shards, `DemoTests.Shard0` to `DemoTests.Shard15`, each a share of the cases in one process, the control smokes and the scenario entries, **eight at a time**. So a test must be safe beside others: it takes a temporary name from `tests/Common/TempPath.h` and never a fixed one, listens on port 0 and not on a number it picked, polls with a time limit and never sleeps a fixed time, and a test that cannot share something, like the smokes that start a game, takes a `RESOURCE_LOCK` in CMake. Narrow the run:

<!-- command -->
```bash
ctest --preset macos-debug -R BagiDemo
ctest --preset macos-debug -L scenario
ctest --preset macos-debug -L unit -j1
```

`-R` takes a name pattern, `-L` a label: `unit` for the gtest shards, `smoke` for the control smokes, `scenario` for scenario files. `-j1` runs one at a time, which is how you tell a test that depends on its neighbours from one that does not. A shard that fails prints its gtest output; re-run the case alone with the binary, as above. The `build` skill is the agents' way to do the same; you do not need it.

## What you did

You read a test of a scene that runs without a window and advances the game by hand, broke an example on purpose and read the failure, ran the smoke of the real program and a scenario with a reference picture, and saw how `ctest` runs them eight at a time and what that asks of a test. A test of your own game has the same three kinds, and the demo's files are the ones to copy.

Next (planned): working with agents, how a coding agent reads and edits the same content and runs the same tests.
