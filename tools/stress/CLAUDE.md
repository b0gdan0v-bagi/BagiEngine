# BagiStress: the engine's own 2D stress app

## Overview

`BagiStress` is an executable of the engine repository with an `IGame` of its own (`StressGame`, name `Stress`, namespace `BECore::Stress`) and no game rules: three scenes that load the engine in different ways, chosen by argument. It is the target of the engine's benchmarks (`tools/perf`) and of a control smoke, so a change to the 2D renderer, the scene or the UI is measured without Match3. It lives in `tools/`, not in `games/`: `games/` goes away when Match3 leaves. How a game plugs into the engine: `src/Modules/Games/CLAUDE.md`.

| Scene (`SceneConfig.xml`) | Flag value | Loads |
|---------------------------|------------|-------|
| `StressSprites` | `sprites` | N spinning sprites, one `IRenderer` call each |
| `StressEntities` | `entities` | N nodes with a transform and a tween, nothing drawn |
| `StressUi` | `ui` | one canvas of N text labels, English and Russian |

Each scene is 1920 x 1080 and has a node `Field` for the scene's component and a node `Run` (after it) for `StressRunComponent`.

## Layout

```
CMakeLists.txt        be_add_module(Stress), the reflection, BagiStress, StressTests (no standalone block: not a game of its own repository)
config/               ApplicationConfig.xml (Game mode), SceneConfig.xml: kept at the engine's content version by ContentFilesVersionTest; UIStrings.xml (the `{#key}` labels of the `ui` scene)
src/Stress/           StressSettings (the flags), StressState (what is shown now), StressGame (the IGame), StressGameControl (the control adapter), SpriteField and EntityField and UiField (pure placement of the sprites, the orbits and the labels, and the texts of the labels), StressHash (the integer hash they share), StressRecorder (the samples, the percentiles and the JSON line of a run), RampController (the pure search of the ramp and its line)
src/Stress/Components/ the component of each scene (StressSpritesComponent, StressEntitiesComponent, StressUiComponent, StressRunComponent: the measurement, fixed and ramp)
tests/                gtest sources of StressTests; StressSceneFixture.h is the shipped scene file on a RecordingRenderer with the StressGame active (the setup of every scene component's test)
```

## The arguments and the state

- **The flags** reach the game through `CoreManager::GetCommandLine()` (`BECore/Application/CommandLine.h`; `Application::SetArguments` fills it), never through `Application`, so a test sets them. `StressGame` parses them in its constructor, because `GameManager` asks `GetSceneName()` before `Start()`. `ParseStressSettings` documents every flag; a wrong one is an error that names the flag (a bad value, an empty value, a value on a bare flag, no value on a valued one), the game logs it once as a warning and starts with the defaults. Nothing is defaulted silently inside the parser.
- **`StressState`** is the live scene, count and language. Every component and the control adapter read it and rebuild what they show only when `Version()` moved; a change to an equal value does not move it, and it never goes back (it starts at 1, so a component's own 0 means "nothing built yet"). A count is `0..kMaxCount` here (the command line and the control API ask for at least 1).
- A component reaches the game through `StressGame::GetActive()` and does nothing when it is null (the game is not the active one).

## Control API

`StressGameControl` (`AsControllable()`, owned by `StressGame`) drives the `StressState` by reference; there is no model in a scene. State `{"scene", "count", "language", "maxCount", "scenes"}`, actions `set_scene`, `set_count`, `set_language` (the exact shapes are in the header). Rules that a controller can lean on:

- `list_actions` shows the other scenes, the half and the double of the count and the other language; `perform_action` takes any `count` in `1..maxCount`, not only the listed ones.
- A malformed or out-of-range value is `invalid_params`; an action that is well-formed and changes nothing is accepted (the version does not move); `illegal_action` is only for an unknown `type`.
- A scene change makes the game switch the active scene (`SetActiveScene` through a callback, so a test needs no scene manager). `reset` takes an optional `{"scene", "count"}` and puts the rest to the defaults, so a measurement starts from a known state; the options are checked before anything changes.
- `get_state` version is `StressState::Version()`.

## The `sprites` scene

`SpriteField` (pure) places sprite `i` of `count`: position, size, spin and the cell of the atlas depend on the index alone (`StressHash.h`: an own integer hash, never `std::rand` or the `std::` distributions: the numbers must not differ between standard libraries), so a bigger count keeps the first sprites where they are. The atlas and the blend mode are what a batcher sees: by default consecutive sprites share them (`count / atlases` per atlas, `kBlendRunLength` per blend mode: the way a game sorts), `--stress-interleave` changes both on every sprite (the worst case). So the runs depend on the count, and a count change re-applies atlas and blend mode to the kept nodes (once, not per frame).

`StressSpritesComponent` makes `count` child nodes of its node (`TransformComponent` + `SpriteRendererComponent`, one shared name) when `StressState::Version()` moved: it appends at the tail or removes from the tail, never rebuilds. The atlases are made once at the first update with `IRenderer::CreateTexture` (`SpriteField::MakeAtlasPixels`; the renderer can be missing at `OnAttached`) and handed to the sprites with `SetTexture`; at `ApplicationCleanUpEvent` it waits for the GPU and lets go of them before the renderer dies. A steady frame only adds `spin * deltaTime` to each `_rotation` through cached pointers. There is no batching: one `IRenderer` call per sprite, which is the number a batcher is to lower.

## The `entities` scene

`StressEntitiesComponent` makes `count` child nodes with a `TransformComponent` each and nothing else, so the scene draws nothing and costs the update of the nodes, the tweens and the draw system's walk. The nodes grow and shrink at the tail like the sprites'. Each node runs one `Tween<float>` of 0..1 (`EaseType::Linear`, the duration is one turn of its orbit, 2..6 s by index, `EntityField::OrbitOf`) whose setter writes the position on a circle of its own; the `onComplete` starts it again. A node's first tween starts at a hashed point of its turn (`EntityOrbit::startTurn`, duration the rest of the turn), so the ends of a field just built are spread over the first turn like the restarts after it (a full turn from 0). Every 16th node also gets a rotation on `SceneUpdateEvent`.

- **The tween rules that matter** (`Animation/CLAUDE.md`): a restart costs one frame per link (rule 2); a shrink resets the tail's `ScopedTween`s before the nodes go (rule 8), so the tween count drops with the count and a destroyed scene leaves none.
- **Tweens restart every frame at the counts the benchmarks use** (some tween ends in nearly every frame: the nodes' orbits last 2..6 s), and a restart is a `TweenManager::Start` (a new handle and an `eastl::function`), so no entities frame is allocation-free. The scene measures the tween manager as a game uses it, restarts included.
- **The cost of a frame grows with the age of the field at large counts:** at 200000 nodes `workMs` p50 is 16 ms in the first 10 to 30 frames after the build and 23 ms from about 200 frames (3 s) on; at 30000 nodes it is the same at 120 and 500 warm-up frames. The ramp runs about a thousand frames, so its later steps already carry that cost; a fixed run at a large count needs a long `--stress-warmup` to measure the steady frame. The cause is not established.
- **The tweens run on the global `TweenManager`, not on the scene:** they keep running when another scene is shown. A run is one scene per process (the benchmarks start a fresh one), so switching scenes with `set_scene` in one run leaves the entities' tweens in the measure of the next scene.

## The `ui` scene

`UiField` (pure) lays N labels on a grid and picks their texts, both from the index and the count alone. The columns follow the aspect of the view and `kUiCellAspect` (a label is a sentence, so a cell stays wide whatever the count), the font size follows the row (14..28), and the grid always fits the view. The text of label `i` is a view of a static table: the even labels are English sentences, the odd ones Russian, from pools of 16 each (so the glyph atlas stays small), every 8th is a `{#stress.label.<k>}` key of `config/UIStrings.xml` (the labels a language switch rewords) and the 5th and the 8th of every eight name a `{count}` or `{last}` variable. `{last}` (the index of the last label) stands where a per-label `{index}` would: `UIVars` are global, so one value cannot be a label's own index.

`StressUiComponent` sits on the node `Field` beside its `UICanvasComponent` (`Screen`, `ScaleWithScreen` 1920 x 1080, no elements in the file) and adds `UIElement` + `UITextComponent` children of the canvas root when `StressState::Version()` moved: append or remove at the tail, and a count change gives every kept label its cell again, because the grid changes with the count. One `Invalidate()` per change, the variables set with the count (an equal value re-expands nothing). A language change is one `UIStrings::SetLanguage`: the strings' own version rewords the key labels at the next draw, no label is touched. A steady frame does nothing in the component; the cost is the canvas's layout cache, the text layout cache and the glyph quads, which is what the scene measures. The text is `wrap="false"` with the default ellipsis: a cell that is too small shows less, and the layout validator would report such findings, which is expected for a stress field and never asserted.

## Fixed mode

`--stress-frames=F` makes `StressRunComponent` (the node `Run`) measure: the warm-up, F recorded frames, one JSON line on stdout, then `QuitEvent`. Without `--stress-frames` or `--stress-ramp` (`StressSettings::IsMeasuring`) a run only shows the scene; the default `frames` alone starts nothing. A `Stress objects` counter (the state's count) is set every frame in every mode, for the overlay and the smoke.

- **Which frames count.** The component's update runs inside a frame and reads the `FrameStats` of the **last finished** one, once per frame id; a held frame is left out. When `StressState::Version()` moves, the frame that notices it is the one that builds the nodes, textures and glyphs, so it and the next `warmup` frames are skipped, and the samples taken so far are dropped: a change of scene, count or language restarts the run. The samples buffer is reserved then (`StressRecorder::Reserve`), so recording allocates nothing.
- **The measured number is `workMs`**: `cpuMs` minus the `Present` stage (headless the present is the software renderer's copy; with vsync it waits for the display). `frameMs` is the end-to-end time: on a manual clock it includes the idle between `step`s, so the line says `manualClock` and nothing gates on it. All three are read from `CoreManager::GetFrameStats()`, never from a wall clock.
- **Percentiles are nearest rank** (`NearestRank`: `sorted[ceil(p * n) - 1]`; one sample is every percentile): a reported p99 is a frame that happened, never an interpolation. p50 and p99 of `frameMs`, `cpuMs`, `workMs`; the means of the draw counters and allocations. `allocsPerFrame` is 0 unless `allocTracking` (`ENGINE_PROFILER_ALLOC`).
- **The line** (`FormatJsonLine`, keys sorted as `headless_frames.py` writes them): `scene count frames warmup interleave atlases manualClock allocTracking`, `frameMsP50/P99 cpuMsP50/P99 workMsP50/P99`, `cpuMs workMs drawCalls triangles textureSwitches allocsPerFrame allocBytesPerFrame`; `frames` is the samples taken. Written by hand (the app does not link nlohmann), numbers as plain decimals. It goes out with `fputs` and `fflush` because it is not a log line: the perf runner reads the last stdout line that starts with `{` (`tools/perf/CLAUDE.md`). A test gives the component a sink (`SetLineSink`) and a clock (`FrameStatsCollector::SetClock`).

## Ramp mode

`--stress-ramp` (it wins over `--stress-frames`) finds the largest count whose frames fit the budget and prints one line, `objectsAt60fps` being the number per scene and machine. `RampController` is pure (no clock, no scene): `StressRunComponent` feeds it the measure of every new frame (`workMs`, or `cpuMs` with `--stress-measure=cpu`) and sets the count it answers into `StressState`. `StressGame` puts `kRampStart` into the state when `--stress-ramp` is given, so a big `--stress-count` is never built first. The algorithm is written in the header; the reasons:

- **A step is 10 settle frames, then 30 measured:** the frame that builds a new count (nodes, textures, glyph pages) is the slow one, and a change reaches the scene a frame late, so the settle frames cover it. No warm-up logic is needed in the component.
- **p95 of the measured frames, nearest rank** (the recorder's `NearestRank`): one slow frame (the OS, a glyph page) must not end the ramp, so not the max; a 60 fps game loses its frame to the tail, so not the mean. Of 30 frames the 29th: one outlier is forgiven, two fail the step.
- **Grow x1.5, then bisect to 2 %**, so a result costs about twenty steps and is within 2 % of the cliff; the cliff can be anywhere, so nothing smarter than a search over a monotone cost is assumed.
- **The result is re-measured once more:** the bisection ends on a count that passed once, and one pass can be noise. A result that fails its re-measure steps down by one resolution (2 %, at least 1) and is measured again, at most three times, then published with `verified: false`. A noisy machine therefore never publishes a count the frame does not hold without saying so.
- **`capped`** (kMaxCount fit: the app cannot ask for more), **`aborted`** (`kMaxRampFrames` frames went first: the best passing count so far, `verified: false`) and `objectsAt60fps` 0 (even `kRampStart` fails) are all in the line. The budget is 16.6 ms, a frame that fits 60 fps.
- **The line** (keys sorted): `aborted allocTracking atlases budgetMs capped frames interleave manualClock measure measureFrames mode objectsAt60fps scene settleFrames steps verified`. `frames` is the frames the ramp took, `steps` the steps (settle + measure). Same stdout rule as the fixed line. `FormatRampLine`.
- The count of a ramp is a number of **this** scene's objects: the three scenes do not compare with one another.
- **A `verified: false` count** is a lower-confidence number: read it with `steps` and the repetitions, and do not gate on it (the ramp benchmarks have no gate).

## Running it

- **The perf runner** drives it through `tools/perf/stress_frames.py` and `.claude/rules/perf-project.md` (`stress-*`, `stress-*-ramp`); the sprites benchmark counts few sprites because the software renderer's present dominates `cpuMs` at more.
- **The control smoke** is the ctest entry `BagiStressControlSmoke` (`tests/stress_smoke.py`): a fresh game per scene on the manual clock, the count shown in the next frame (`frame_stats` draw calls and the `Stress objects` counter), the bad values, screenshots that are not flat, a language switch that changes the `ui` picture, `set_scene`, and a log with no Warning or Error. It imports the helpers of `tools/control/tests/smoke.py` and starts the game through `GameProcess` (the log check needs the log's path, which `smoke.play` keeps to itself).
- **Not there:** 3D, a comparison of backends, a URL query in the browser build (the page runs the defaults: `sprites`, the default count).
