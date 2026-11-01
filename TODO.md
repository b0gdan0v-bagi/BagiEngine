# TODO

Known problems that are not being worked on yet. One entry per problem: where it is, what is wrong, what a fix has to cover. A finished entry is deleted, not marked done.

**Entry fields.** `tools/todo_sync/` reads this file to build a progress dashboard and to export the entries to a task tracker (its `CLAUDE.md` has the commands). The entry id is the slug of its heading, so renaming a heading makes a new entry for the tracker. Every field below is optional and sits on its own line near the top of the entry:

- `**Status:**` starts with one keyword, `open` (the default), `in progress` or `deferred`, then a date `YYYY-MM-DD` and free text;
- `**Stage:**` the roadmap number (1 to 6); a problem outside the roadmap order has none;
- `**Plan:**` the path of a plan under `.claude/plans/`; its `- [ ]` / `- [x]` lines give the progress.

## Roadmap

Direction for the engine, not a problem entry. The engine has strong infrastructure (reflection, layered config, TaskSystem, ImGui editor, tests) and no runtime for a real 2D game: no animation, text, audio or input mapping. The entries below follow this order. Every "missing" statement was found by reading the code, not by running a game.

1. **Hygiene:** CI is deferred by the project owner (entry below; the Linux job in the test-gaps entry waits for it). The link from this file to a task tracker and a dashboard is done: `tools/todo_sync/`.
2. **External control API (done on macOS):** a model or a program plays a game over MCP / WebSocket (`tools/control/CLAUDE.md`, `src/Modules/RemoteControl/CLAUDE.md`); what is left (Windows, a capture on Vulkan, the Stockfish games in CI, the clock of delayed tasks) is the entry below.
3. **Minimum for a 2D game, in this order:** animation, 2D rendering, text, audio, input mapping, in-game UI (and the choice of how its screens are authored, entry below).
4. **Owner decisions:** renderer backends (the Android and web targets in the release entry weigh in), what ECS means in this engine.
5. **Content and quality:** prefabs, saves / hot-reload / packing, test gaps and platforms, the reflection generator.
6. **Third game and releases:** Mahjong, then release builds for PC, Android and web.

**Not now:** networking for players (servers, multiplayer; the control API in stage 2 is a local development tool, not this), 3D, a physics engine of our own (take Box2D when physics is needed), a scripting language, and a rewrite to pure ECS (reflection, the inspector and serialization are built around `IComponent`).

**Driver:** the third game is Mahjong solitaire (entry below), so that stage 3 has a concrete consumer: animation (a pair leaves the board), text (timer, score), audio (a match sound), input mapping (mouse and touch) and in-game UI (new game, shuffle, hint). It does not need a camera, movement with `dt` or a gamepad, so those wait for a game that needs them. Make Match3 animated as the regression check. Stages 1 to 3 are done when tiles and gems animate, the score and the timer are drawn with an engine font, a match plays a sound, input goes through an input map, and CI builds and tests it on macOS and Windows (CI is deferred, so that last point waits for its entry).

## Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve

**Where:** `src/Widgets/ConfigEditor/ConfigEditorWidget.cpp:22` (`schema.RegisterEnum<LogSinkType>("LoggerConfig"_intern, "sink/@type")`), `ConfigFieldSchema.h` (`RegisterEnum`), `FieldInspectors.cpp` (`RenderEnum`), `src/Modules/BECore/Logger/LogSinkType.h`.

**What is wrong** (found by reading the code, not reproduced in a running editor):

1. The dropdown options come from `EnumUtils<LogSinkType>::Names()`: `Console`, `File`, `Output`. `config/LoggerConfig.xml` stores the sink **class names**: `ConsoleSink`, `FileSink`, `OutputSink`. `RenderEnum` looks for the current value among the options, finds nothing, `selectedIndex` stays `-1` and the combo shows no current selection.
2. Choosing an option and saving writes `type="Console"` into the file. On the next load `LoadElement` (`Reflection/DataAccessor.h`) creates the sink with `AbstractFactory<ILogSink>::Create(typeString)`, which looks the type up by the hash of the class name. `"Console"` is not registered, the result is null, and `ILogSink` is abstract, so the entry is dropped without any message. If every entry is dropped, `LoggerManager` silently falls back to a `ConsoleSink`.
3. `LogSinkType` is defined twice: by hand in `BECore/Logger/LogSinkType.h` (`Console, File, Output`) and by the generator in `build/Generated/EnumLogSink.gen.hpp` (`File, Console, Output`, different order). Only the handwritten one is included anywhere; including both in one translation unit would be a redefinition, and the value order already disagrees.

**A fix has to cover:**

- the dropdown lists exactly the strings the factory accepts, and shows the current value as selected;
- one definition of the sink type enum (or none: the editor can take the registered class names from `AbstractFactory<ILogSink>::GetRegisteredTypes()`);
- a visible error when a config names a sink type that is not registered, instead of dropping the entry silently;
- a check that edit → save → restart keeps every sink working (a round-trip test of `LoggerConfig` through the editor's save path, or at least through `ConfigManager::SaveConfig` / `ReloadConfig`).

The same pattern (an enum hint whose names differ from what the loader expects) can exist for other registered fields: `LogLevel` for `sink/@minLevel` is fine because `minLevel` is read through `EnumUtils`, but check any new `RegisterEnum` call against the loader that reads the attribute.

## No CI: builds and tests run only on a developer machine

**Status:** deferred by the project owner on 2026-10-31. Skip it when taking the next roadmap item; the entry stays as the record of what a fix has to cover.

**Stage:** 1

**Where:** the repository has no `.github/` and no other pipeline definition. Presets are in `CMakePresets.json`, tests run through `ctest --preset ...`.

**What is missing** (found by reading the repository, not by running anything):

1. Nothing builds or tests a push. The engine supports macOS and Windows, so a break on the other OS goes unnoticed until someone builds there.
2. The Vulkan loader is vendored only in part: `libvulkan.1.dylib` (macOS) and `vulkan-1.lib` (Windows) are tracked, the `*.dll` files are git-ignored and have to be provided by hand (`external/Vulkan-Loader/README.md`). A CI runner on Windows needs an answer for the runtime DLL, if the test and game executables load it.

**A fix has to cover:**

- a workflow for macOS (`macos-debug`) and Windows (one MSVC or Clang preset) that checks out the submodules (`git submodule update --init --recursive`, `external/googletest` included), configures, builds the game and test targets and runs `ctest`;
- a decision for the Vulkan loader DLL on the Windows runner (install the SDK, fetch the loader, or build without the Vulkan backend), after checking whether the test executables need it at all;
- a guard on the number of tests (669 ctest entries at the last count, from `ctest --preset macos-debug -N`; the pytest suites of `tools/` come on top: 93 in `tools/control/tests`, 49 in `tools/todo_sync/tests`): it must not drop;
- a Linux job once Linux presets exist (see the entry on test gaps and platforms);
- once the games have their own repositories, the workflow builds the workspace repository (checkout with submodules), not the engine alone (see "Games live in the engine repository").

## Control API: checked on macOS only, no capture on Vulkan, Stockfish games run by hand

**Status:** open since 2026-11-01. The control API is done and tested on macOS: the command queue, the game interface with the Match3 and Chess adapters, the WebSocket server, the manual clock with `step`, headless mode with `screenshot`, the MCP server and the Stockfish bridge (`BECore/Control/CLAUDE.md`, `RemoteControl/CLAUDE.md`, `tools/control/CLAUDE.md`). Measured there: Chess against Stockfish 19 to a mate with every position cross-checked, and 11,274 positions of random games with no difference in the legal moves. What is left is below.

**Stage:** 2

**Where:** `src/Modules/RemoteControl/` (Winsock code path), `src/Modules/SDL/` (the `dummy` driver and the software renderer), `src/Modules/Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp` (`RequestCapture`), `tools/control/`, `src/Modules/TaskSystem/src/TaskSystem/TaskScheduler.cpp` (`ScheduleDelayed`).

**What is left:**

1. Windows is unchecked: Winsock and `ws2_32`, the `WinMain` arguments, the `dummy` video driver with the software renderer, and the Python tools (`game_process.py`; the tests that make a fake executable with a shebang are skipped there) have not been built or run on it.
2. The Vulkan backend cannot be seen or run without a window: `--headless` and `screenshot` work on the SDL software renderer only, and `VulkanRendererBackend::RequestCapture` answers with an error (MoltenVK has no headless surface on macOS). Whether that gap is worth closing depends on the decision about the renderer backends.
3. The Stockfish games run by hand: `tools/control/tests/test_stockfish.py` plays real games when Stockfish and a built `BagiChess` exist and skips otherwise, and pytest is not part of ctest, so nothing runs it on a push.
4. Delayed tasks use the wall clock: `TaskScheduler::ScheduleDelayed` computes `Clock::now() + delay`, so `co_await Delay(...)` and `TaskManager::RunDelayed` do not follow `Time::Advance`. A timer or an animation built on them would run in real time under `--manual-clock` and break the determinism the manual clock is for. Nothing in a game uses them yet (only `TaskSystemTest`).
5. A person playing against Stockfish at the window was not tried (the bridge waits for the person's move by polling `get_state`).

**A fix has to cover:**

- the `RemoteControl` tests, the capture tests, the smoke tests and `tools/control/tests` built and run on Windows;
- a capture on the Vulkan backend, or the decision that the SDL software path is the only one to test on;
- the Stockfish games as a ctest entry or a CI step (see the CI entry);
- delayed tasks driven by game time (feed the scheduler from `Time`, or add a game-time delay beside the wall-clock one) before the animation work uses them.

## Third game: Mahjong solitaire

**Stage:** 6

**Where:** `games/Chess` and `games/Match3` (the models to follow), `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`), `src/Modules/Games/CLAUDE.md` (the engine-game contract), `src/Modules/Games/src/Games/IControllableGame.h`.

**What is missing:** the game. Assumed: tile-matching solitaire (Shanghai), one player. The four-player rules game needs AI players and is out of scope. It is the consumer that the roadmap "Driver" asks for.

**A fix has to cover:**

- `games/Mahjong/` built like the other two (after the games leave the engine repository, a new repository made like Chess, see "Games live in the engine repository"): an `IGame` registered in `AbstractFactory<IGame>`, a rules model with no rendering and no animation, an executable `BagiMahjong`, `MahjongTests`; no engine code names the game;
- the rules as pure code with gtests: layouts of stacked layers, the free-tile rule (nothing on top, and the left or right side open), pair matching (flowers and seasons match within their group), a deal that is always solvable (generated by playing backwards), shuffle when no move is left, hint, undo, a seeded platform-independent RNG like Match3's;
- a control API adapter (`IControllableGame`): the state and the list of free pairs as actions, so a model can play and test it;
- stacked tiles use the node `layer` that the draw pass already sorts by (`SceneNode::SetLayer` at runtime, `Scene/CLAUDE.md`, "Draw order"): one node per tile, the layer from its height in the layout. No renderer work is needed first;
- the features it consumes: animation, text, audio, input mapping with touch, in-game UI, save game (resume a deal);
- the first game to go through the release entry on all three targets.

## Games live in the engine repository: move them out and add a workspace repository

**Status:** deferred, 2026-11-01. Planned, nothing moved yet.

**Where:** `games/Chess`, `games/Match3`, `games/CMakeLists.txt`, `CMakeLists.txt` (`VS_STARTUP_PROJECT`), `CMakePresets.json` (`targets` of `build-base`), `CI/BagiEngine.code-workspace` (launch and task entries), `tools/control/stockfish_bridge.py` and `tools/control/tests/test_stockfish.py` (Chess by nature), `.claude/rules/cmake-rules.md`, `.claude/rules/tests-module.md`, `src/Modules/Games/CLAUDE.md` ("Adding a game"), `WORKSPACE_README.md` and `launcher_config.json` (what the PyQt launcher builds).

**What is missing** (found by reading the code, not by building):

1. Each game's `CMakeLists.txt` has a standalone block (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` → `project()` + `include(engine/cmake/BagiEngine.cmake)`), and the engine side is ready for it (`BAGI_ENGINE_ROOT` comes from the location of `BagiEngine.cmake`, `games/` is guarded by `EXISTS`, `be_add_game_control_tests` passes `BAGI_ENGINE_DIR` to a game's script). Nobody has built a game as the top-level project, so the block is untested.
2. There is no place that builds and tests every game once the games leave. The engine repository alone will build no game.

**A fix has to cover:**

- a rehearsal first, with no change to the repository: copy `games/Chess` next to an engine clone (scratchpad), configure, build `BagiChess` and `ChessTests`, run `ctest` and `BagiChessControlSmoke`; write down every break (paths from `CMAKE_SOURCE_DIR`, presets, `.clang-format` lookup, PCH, `build/Generated`, the Vulkan loader copy) and fix it in the engine before the split;
- Chess first, then Match3: `git filter-repo --subdirectory-filter games/<Name>` on a fresh clone (history kept), the engine as the submodule `engine/`, a `CMakePresets.json` that includes the engine's, `.clang-format` / `.gitignore`, the `.claude/` skills and rules the game needs, and for Chess the Stockfish bridge and its test; configure, build, tests and a run must pass in the new repository. The owner creates the repositories and pushes;
- a workspace repository (`BagiWorkspace`) with the submodules `engine/`, `games/Chess`, `games/Match3` and its own top-level `CMakeLists.txt` and presets: one `cmake --build` builds every game, one `ctest` runs every test. The games' own `engine/` submodules stay uninitialised there (`update = none`), so there is a single engine checkout and an engine change is tested against all games at once. The engine stays free of game names: the workspace is a separate repository, not `games/` submodules of the engine (that would also make a submodule cycle);
- the engine cleanup, after the games build in their repositories: `git rm -r games/`, the `targets` list, the IDE workspace file and the docs above, with `BagiEngineTests` building and passing without any game; "Adding a game" becomes "make a repository like Chess";
- the build commands in `CLAUDE.md` and the `build` skill (the engine repository no longer builds a game; say where), and a note on the `engine/` workflow in each game repository (commit and push inside `engine/`, bump the pointer in a separate commit);
- the CI entry then builds the workspace (`git submodule update --init --recursive`), and the Mahjong entry starts as its own repository.

## Running several games from one editor: choose a model

**Status:** deferred, 2026-11-01. Revisit once the workspace repository from the entry above exists.

**Where:** `src/main.cpp` (one executable is one game), `cmake/BagiEngine.cmake` (`be_add_game_executable` bakes `BAGI_GAME_ROOT` in at build time and links the game with `WHOLE_ARCHIVE`), `src/Modules/Games/` (`GameManager::SwitchTo` and `GetAll()` already handle several registered games, no widget exposes them), `src/Modules/BECore/FileSystem/` and `Config/` (the engine and the game layers are fixed at startup).

**What is missing:** a way to start or switch between games without choosing the executable. Needed: a place to build and test every game (the workspace entry above) and, maybe, one window that runs any of them. Options, cheapest first:

1. **Launcher only:** the workspace builds every exe; a VS Code launch configuration, a ctest group or a script starts one. No engine change.
2. **One executable with every game linked,** the game chosen at startup (an argument or a config) or at runtime: `AbstractFactory<IGame>` and `GameManager` already cope; `BAGI_GAME_ROOT` has to become a runtime parameter and the `FileSystem` / `ConfigManager` layers have to be rebuilt on a switch (a restart on switch is the cheap form).
3. **Games as shared libraries** loaded by an editor executable: a hot switch, the most work (ABI across the boundary, `Singleton<T>` and EASTL across modules, registration order, reflection).

**A fix has to cover:**

- the choice of model; recommendation: option 1 with the workspace, option 2 only when a person needs to switch games inside one window;
- for options 2 and 3, what a switch does to the scene, the textures, the input subscriptions and the control API (the `list_games` / `switch_game` methods exist, `Games/CLAUDE.md`);
- the Stage of the roadmap that owns it, if it joins the roadmap.

## Chess: no game of a person against an engine and no analysis; Stockfish exists only in an external script

**Status:** open since 2026-11-01. Asked for by the project owner after the control API work: play a real person against Stockfish in the game itself, and then go deeper (analysis and the rest). It needs text and in-game UI first (the two entries below), so it waits for roadmap stage 3.

**Where:** `games/Chess/src/Chess/Game/ChessGameModel.h` (`OnSquareClicked` is the path of a person, `TryMove` the path of anything else; the model has no notion of a side played by a program), `games/Chess/src/Chess/Components/` (the board, the pieces and the result bar), `games/Chess/src/Chess/Game/Position.h` (`DoMove` / `UndoMove` exist), `tools/control/stockfish_bridge.py` (the only place Stockfish is used), `src/Modules/TaskSystem/` (a search has to run off the main thread).

**What is missing** (found by reading the code and searching `src`, `games` and `tools`, not by playing):

1. A person cannot play the computer: both sides of the game are mouse clicks. Stockfish plays only through `tools/control/stockfish_bridge.py`, which needs the control API switched on, Python and an installed Stockfish, and a person at the window on the real clock (not tried yet, see the control entry). It is a test and development tool, not a feature of the game.
2. There is no engine inside the game, and nothing in `src` or `games` starts a process (a search for `popen`, `posix_spawn`, `CreateProcess` finds nothing). Stockfish is GPL-3.0 and the repository has no license file, so what linking or shipping it would require is not settled. The web target of the release entry cannot start a process and Android can do it only in ways an app should not rely on, so an engine for them has to run in the game's own process.
3. There is no analysis of any kind: no evaluation, best move or lines, no move list on the screen, no takeback, no stepping through the game, no resign or draw offer, no clocks, no PGN. The model keeps `GetStartFen()` and `GetMoves()`, which are enough to rebuild any earlier position, and `Position::UndoMove()` exists, but no player action uses them. The engine can draw text now (`TextComponent`, `BECore/Text/CLAUDE.md`), but there is no in-game UI to lay out a move list, a number or an opening name (entry "No in-game UI").
4. The choices a person needs have no place to live: which side to play, the strength of the opponent, a new game, resign, and the piece of a promotion (a click on the last rank always plays the queen; the other pieces are reachable only through `TryMove` and UCI).

**A fix has to cover** (a direction to confirm with the owner, not a decision):

- **the opponent:** an engine interface in `games/Chess/` ("a move for this `Position` within this time or strength") that runs the search off the main thread through `TaskManager` and plays the answer with `TryMove` on the main thread, so the model and the scene stay as they are. Behind it, in this order of recommendation: (b) an engine of our own in `games/Chess/`, an alpha-beta search over the existing `MoveGenerator`, at the level of a casual player, in the game's process on every target and deterministic for a fixed depth (the manual clock and the smoke tests need that); (a) a UCI engine as a child process on desktop, with the path to Stockfish chosen by the user and nothing shipped, which is the logic of `UciEngine` in the bridge ported to C++; (c) Stockfish compiled in, only after the license of the project is decided;
- **the controls of a game:** side, strength, new game, resign, takeback (rebuild from `startFen` + `moves`, or `UndoMove`) and the promotion choice, on the in-game UI and the text of the roadmap entries; the opponent's turn is shown (the game must not look frozen while it thinks);
- **analysis** (needs the UCI engine of (a) for anything deeper than a basic evaluation of our own): an evaluation bar and a best-move hint after each move; a move list that can be clicked to view an earlier position without changing the game; a review after the game that evaluates every position in the background and marks the moves that lost the most; PGN export and import; opening names from a table. `MultiPV` and `go depth` give the lines;
- **the control API and tests:** the opponent as state of `ChessGameControl` (its side and strength, and "thinking" in `get_state`), so a model can play a game against the engine of the game itself; gtests of the engine on known positions (a mate in one and two, a hanging queen, a draw by repetition); a smoke scenario of a whole game against it; `stockfish_bridge.py` stays as the independent referee of the rules and as the yardstick for the strength of our own engine (a match of a fixed number of games at a low `Skill Level`).

## No animation: no tweens, easing or timelines

**Stage:** 3

**Where:** `games/Match3/CLAUDE.md` ("`Match3GameModel` never animates"), `games/Match3/src/Match3/Components/Match3BoardComponent.cpp`, `src/Modules/TaskSystem/` (coroutines that animation could use).

**What is missing** (found by reading the code, not by running a game): no way to change a property over time. Match3 resolves a whole cascade instantly, and Chess pieces jump to the target square. The frame time it needs exists now (`Time`, `BECore/CLAUDE.md`).

**A fix has to cover:**

- a tween API for float, vector and color properties with easing functions, cancel and completion callbacks, ticked by `dt`;
- use from coroutines (`co_await` a tween) through the TaskSystem on the main thread;
- Match3 as the first consumer (swap, fall and refill animation) while the model stays animation-free (the rule in `games/Match3/CLAUDE.md`);
- gtests for the easing math and for tween ordering with a fake clock.

## 2D rendering is minimal: no rotation, camera or blending

**Stage:** 3

**Where:** `src/Modules/BECore/Renderer/IRenderer.h` (`DrawFilledRect`, `DrawTexture` are the whole drawing surface), `SDLRendererBackend.cpp`, `Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp`, `Scene/Components/TransformComponent.h` (position and size in scene units, which are output pixels unless the scene sets a view size).

**What is missing** (found by reading the code, not by running a game):

1. Layers exist, but only per node: `SceneNode::GetLayer()` plus the scene order sort the draw pass (`Scene/CLAUDE.md`, "Draw order"). A child does not inherit its parent's layer, and there is no sort key finer than the node (a sprite and a quad on one node cannot interleave with other nodes).
2. No rotation or scale of a sprite, no flip. (A tint with alpha, `IRenderer::PushClipRect()` and `CreateTexture()` exist: `rendering-module.md`, "Drawing primitives".)
3. No movable 2D camera: a scene can fit a fixed view size to the output (`viewWidth` / `viewHeight`, `Scene/CLAUDE.md`, "View size"), but scrolling and zoom need a camera transform on top of that.
4. One blend mode only: straight alpha over the target. No additive or multiply.
5. A node has only absolute pixel coordinates, no parent-relative transform.

**A fix has to cover:**

- each capability first in `IRenderer` / `TransformComponent`, then in both backends (`.claude/rules/rendering-module.md`; a backend that cannot do it documents the fallback);
- layer inheritance from the parent, if a hierarchy of drawn nodes needs it;
- a camera that maps world to screen, used by the renderer components;
- a decision on the backends before the work starts (see the entry on renderer backends).

## The render output size is fixed at startup: the Vulkan swapchain is never recreated, and there is no HiDPI

**Where:** `src/Modules/Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp` (`AcquireFrame()`: "swapchain out-of-date (TODO: recreate on resize)"; `Present()` ignores the result of `VulkanSwapchain::Present`), `VulkanSwapchain.cpp` (`ChooseExtent`), `SDLEventsProvider.cpp` (the `SDL_EVENT_WINDOW_RESIZED` case sits in a commented-out block, so no resize event is emitted), `config/SDLWindowConfig.xml` (no `SDL_WINDOW_HIGH_PIXEL_DENSITY`).

**What is wrong** (found by reading the code, not reproduced by resizing a running window):

1. The swapchain is created once, with the extent the surface reports at that moment. When the window changes size, `AcquireNextImage` returns "out of date", `AcquireFrame()` returns without recreating anything and the frame is skipped. The compositor may stretch the old image instead, and then the picture is distorted. `Present()` does not look at the result either.
2. Since the scene view fit (`Scene::Draw()`, `IRenderer::GetOutputWidth()`), the picture follows the output size, so a recreated swapchain is all that is missing: nothing else has to learn about the new size.
3. The window has no `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so on a Retina display the output is in window points and the OS scales it up by 2 (measured from a screenshot of `BagiChess`: exactly 2 px per scene unit; not checked in MoltenVK or SDL code). The picture is soft, and screenshots of the control API will be point-sized.

**A fix has to cover:**

- recreate the swapchain, its image views and framebuffers (the render pass only when the format changes) on out-of-date / suboptimal and after a resize, with `vkDeviceWaitIdle` first; skip drawing while the window is minimized (extent 0);
- a decision on HiDPI: the output in pixels with a scale factor for the scene view, or points as today (the editor viewport and ImGui scale differently);
- check `Present` and `AcquireNextImage` results in one place;
- a manual check list (maximize, resize by hand, minimize and restore, move between displays), because no test creates a Vulkan device.

## Chess pieces are drawn stretched: the atlas cell is not square

**Where:** `games/Chess/config/TextureLibrary.xml` (every cell is 116.67x190.5 of `assets/chess_test.png`), `games/Chess/src/Chess/Components/ChessPieceComponent.cpp` (sets the transform to the square size), `Scene/Components/SpriteRendererComponent.cpp` (`DrawTexture` into the whole transform rect).

**What is wrong:** a sprite is drawn into the full rect of its node, and a piece node is a square, so a 116.67x190.5 cell (width to height 0.61) is stretched horizontally by about 1.6x: the pieces look wide and squat. There is no way to keep the aspect ratio of a sprite.

**A fix has to cover:**

- either an aspect-preserving mode in `SpriteRendererComponent` (fit by height and center, which also helps every other sprite; see the 2D rendering entry), or cells cropped to squares in the atlas and the `srcRect` values;
- the legal-move and selection highlights are drawn by the board in the full square, so they must still line up with the piece after the change.

## Match3 does not scale to the window: its scene has no view size

**Where:** `games/Match3/config/SceneConfig.xml` (`Match3Scene` has no `viewWidth` / `viewHeight`), `Match3BoardComponent` (`originX="100" originY="80" cellSize="64"`, 8x8 board).

**What is wrong:** the scene view fit is opt-in, and only `ChessScene` declares a view size. The Match3 board is 512x512 units in a window of about 1632x910, so it fills a small part of it, as the Chess board did. Its HUD is an ImGui window (`ImGui::Begin("Match3")`), shown only where an ImGui context exists (Editor mode; the Game mode widget set has no `ImGuiWidget` and Match3 ships no `GameWidgetsConfig.xml`). It is in window pixels and is not part of the scene fit.

**A fix has to cover:**

- a view size for `Match3Scene` (the board at (100, 80) with the same margin on every side is about `viewWidth="712" viewHeight="672"`, to be checked in the running game);
- where the HUD goes in a mode that has ImGui: its window does not move with the fitted board;
- the score and the Reset button in Game mode: text exists (`TextComponent`), but the HUD still needs a canvas and a button, so this waits for the in-game UI (plan stage 4: `.claude/plans/magical-leaping-blossom.md`).

## No audio

**Stage:** 3

**Where:** no module, no dependency (`external/` has SDL, SDL_image, Vulkan headers and loader, googletest).

**What is missing** (found by searching `src`, `games` and CMake): sound effects and music.

**A fix has to cover:**

- a library choice (SDL3_mixer or miniaudio) and a new module placed in the module order of `CLAUDE.md` without a circular dependency;
- `AudioClip` as a resource through `IResourceLoader`, an `AudioSource` component and playback events (so games trigger sounds without including the library);
- volume and mute in config; tests run without an audio device (a null or dummy driver).

## Input has no mapping layer: actions are just names

**Status:** in progress since 2026-11-01. Stage 2 of the plan (restored events, window metrics, `UIInput`) is done; the action map and the gamepad stay open.

**Stage:** 3

**Plan:** `.claude/plans/magical-leaping-blossom.md`

**Where:** `src/Modules/BECore/Input/ActionsLibrary.h` (a set of action names), `src/Modules/Events/src/Events/InputEvents.h` (`ActionEvent`), `src/Modules/SDL/src/CoreSDL/SDLEventsProvider.cpp` (`ConvertSDLEvent`), `SDLEvents.h` (key, mouse and window event types, no gamepad events), `Scene/Components/ClickableComponent.cpp`.

**What is missing** (found by reading the code, not by running a game):

1. There is no binding from physical input (key, mouse button, gamepad button or axis) to an action. `ActionsLibrary` only lists names, `ClickableComponent` emits the action itself, and keys and gamepads have no path to `ActionEvent`.

**A fix has to cover:**

- an `InputMap` read from XML config (layered, so a game ships its own): key / button / axis → action, with pressed, held and released states and an axis value;
- gamepad support through SDL3 events;
- components that react to mapped actions or raw input subscribe with `SubscribeInScene`, so only the active scene reacts (`ClickableComponent` already does);
- a gtest that feeds raw events and checks the resulting actions.

## No in-game UI: UI exists only as ImGui editor panels

**Status:** in progress since 2026-11-01. Stages 0 to 3 of the plan are done (renderer primitives, text, input, canvases and adaptive layout with the size matrix); stage 4 has started: task 4.1 is done (elements and variants are reflected, a screen can be written in `SceneConfig.xml`); `UIDocument`, the controls and the first screens are next.

**Stage:** 3

**Plan:** `.claude/plans/magical-leaping-blossom.md`

**Where:** `src/Widgets/` (editor widgets), `Scene/Components/ClickableComponent.cpp` (the only clickable thing on a scene).

**What is missing** (found by reading the code, not by running a game): the controls (panel, label, button) and the screens of the games. Text is drawn (`TextComponent`), canvases, anchored and stacked layout, responsive variants, hit testing and the layout validator exist (`src/Modules/BECore/UI/CLAUDE.md`); elements are plain rects built in code and cannot be written in `SceneConfig.xml` yet.

**A fix has to cover:**

- a few controls (panel, label, button with normal / hover / pressed state) on the element tree of `UICanvasComponent`, with `TextComponent`'s layout for the label;
- a main-menu scene in one of the games as the first consumer.

## Game UI has no LLM-friendly authoring path yet: choose one before building it

**Status:** in progress since 2026-11-01. Stage 5 of the plan is the spike that measures the options; the owner decides after it.

**Stage:** 3

**Plan:** `.claude/plans/magical-leaping-blossom.md`

**Where:** `src/Widgets/` (the editor panels are ImGui code), `games/*/config/SceneConfig.xml` and `Scene/CLAUDE.md` (scenes are XML; positions are absolute pixels in `TransformComponent`), the control API entry (a screenshot is planned, not built). No Figma or other UI import exists in the repository.

**What is wrong** (found by reading the code, not by running anything): in-game UI does not exist yet (entry above), so its authoring format is still free to choose. A model can edit XML as text, but it cannot see the result until the screenshot exists, a designer's mockup has no path into the engine, and absolute pixel positions break on other screen sizes (Android, web).

**Options** (a decision of the project owner):

1. **Declarative UI in the engine's own XML** (panel, label, button, anchors), edited as text and checked by a control API screenshot and a scene dump. No new dependency; it fits reflection and the scene editor.
2. **Figma as the source:** (a) the model reads the design through the Figma MCP server and writes the XML itself; or (b) a script in `tools/` imports a Figma file through the Figma REST API (frames to panels, text to labels, components to prefabs, exported images to an atlas) with a layer naming convention as the contract. Both need the text, layout and prefab pieces first.
3. **An HTML/CSS-like UI library** (RmlUi is the candidate; whether it fits all three release targets is not checked). Models know HTML and CSS well, but it is a large dependency with its own layout and text.
4. **Dear ImGui for game UI** stays what it is today: for tools. It is code, not data, and the entry above puts game UI on `IRenderer`.

Recommendation: option 1, built for a model's edit, run, screenshot loop. Option 2(a) can be tried at once with no engine work; 2(b) only when real Figma files exist.

**A fix has to cover:**

- the chosen path recorded in `Scene/CLAUDE.md` (or a UI doc next to the code);
- UI in plain text files with stable node names and no editor-only state, so a model can read and diff them;
- the feedback loop: screenshot and scene dump from the control API, and hot-reload of scene files in Game mode (the saves / hot-reload entry);
- layout (anchors, sizes) computed in pure code and covered by gtests at several aspect ratios, without a window;
- a short "how to build a screen" section with the component list and one example screen in the nested `CLAUDE.md`, the place models read;
- if Figma is chosen: the layer naming convention, the importer in `tools/` and a fixture file in a test.

## Renderer backends: one decision, then batching

**Stage:** 4

**Where:** `src/Modules/BECore/Renderer/IRenderer.h`, `src/Modules/SDL/`, `src/Modules/Vulkan/`, `external/Vulkan-Loader/` (vendored loader for macOS, which runs Vulkan on top of Metal).

**What is wrong** (found by reading the code and docs):

1. Every new `IRenderer` capability has to be written twice, in `SDLRendererBackend` and `VulkanRendererBackend` (`rendering-module.md`). For a 2D engine this doubles the cost of every item in the 2D rendering entry.
2. A draw is one `IRenderer` call per object; there is no batching, atlas support or sort pass in the interface.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- decide which of: (a) the SDL renderer is the main backend and Vulkan stays frozen at its current feature level, (b) keep both and implement new features in one backend first, (c) replace both with the SDL3 GPU API, which gives native Metal on macOS instead of the loader. Recommendation: a 1-2 day spike on (c), and (a) until it is decided;
- after the decision, sprite batching and texture atlases (`TextureLibrary` already maps a sprite name to a texture and a source rect);
- record the decision in `rendering-module.md` and `Vulkan/CLAUDE.md`.

## ECS is nominal: EnTT only hands out entity ids

**Stage:** 4

**Where:** `src/Modules/BECore/Scene/Scene.h` (`entt::registry _registry`), `SceneNode.cpp` (`_entity = scene->GetRegistry().create()`), `Scene/IComponent.h`, `Scene/CLAUDE.md`, `CLAUDE.md` ("modular ECS architecture").

**What is wrong** (found by reading the code):

1. The registry holds one empty entity per `SceneNode`. Components are polymorphic `IComponent` objects stored in the node; there are no registry views, systems or component storage. The architecture description in `CLAUDE.md` promises more than the code does.
2. Rendering is a per-component `OnDraw()` call from `Scene::Draw()`. The pass sorts nodes by layer, but a component still issues its `IRenderer` calls itself, so there is nowhere to batch draws.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- recommendation: keep the object model (`SceneNode` + `IComponent` fits reflection and the inspector) and make rendering a system that collects `TransformComponent` + renderer data from the registry, sorts by layer (the sorted node pass of `Scene::Draw()` is the first step) and hands a batch to `IRenderer`. This gives EnTT a real job and prepares the 2D rendering and batching entries;
- the alternative is to say plainly that this is a scene graph in the Unity style and drop EnTT if ids are all it provides;
- not a rewrite to pure ECS;
- record the decision in `Scene/CLAUDE.md` and correct the wording in `CLAUDE.md`.

## No prefabs: all of a game's scenes live in one `SceneConfig.xml`

**Stage:** 5

**Where:** `games/*/config/SceneConfig.xml`, `Scene/CLAUDE.md` ("Every `SceneConfig.xml` replaces the one below it"), `Scene/SceneManager.cpp`.

**What is missing** (found by reading the docs and configs): a reusable node template and a scene in its own file. A game's `SceneConfig.xml` replaces the engine's whole file, and a node such as a Match3 gem or a Chess piece is spelled out every time it appears.

**A fix has to cover:**

- a prefab: a node (with children and components) in its own XML file, instantiated by name with per-instance overrides, and the same instantiation at runtime (`Scene::CreateNode` equivalent);
- scenes loaded from separate files listed in `SceneConfig.xml`, keeping the layering rule (a game's file wins);
- the scene editor saving prefab instances without flattening them.

## The scene editor saves only the active scene

**Where:** `src/Widgets/SceneEditor/SceneEditorWidget.cpp` (`SaveScene()`).

**What is wrong** (found by reading the code, not by saving in the editor): `SaveScene()` removes the whole `<scenes>` element of `SceneConfig` and writes back one `<scene>`, the active one. A config with two scenes (the engine's own file has a debug `MainScene`; a game with a menu would have more) loses every other scene on the first save. The same code writes the scene attributes by hand (`type`, `name`, `viewWidth`, `viewHeight`), so a new reflected field of `Scene` is dropped on save unless it is added there.

**A fix has to cover:**

- replace only the `<scene>` element with the active scene's name and keep the others as they are;
- write the scene attributes through the reflection (`Scene::Serialize`) instead of by hand;
- a round-trip test of the save path (load, edit, save, load) with two scenes.

## No save games, hot-reload or asset packing

**Stage:** 5

**Where:** `src/Modules/BECore/Reflection/BinarySerializer.h` / `BinaryDeserializer.h` (format in `.claude/rules/reflection-module.md`), `src/Modules/BECore/FileSystem/`, `Resource/`.

**What is missing** (found by reading the code, not by running a game):

1. `BinarySerializer` has no consumer: a game cannot save and restore its state.
2. Changing a texture or config needs a restart; there is no file watcher.
3. Assets are loose files, with no packed format for a release build.

**A fix has to cover:**

- a save game service built on `BinarySerializer` (versioned, name-keyed as the format already is) with a Chess or Match3 model as the first consumer;
- hot-reload of textures and configs in Editor mode through `FileSystem` and `ConfigManager::ReloadConfig`;
- later: a pak file behind the virtual `FileSystem` mount points, so a release reads packed assets through the same paths.

## No release builds: games run only as developer executables

**Stage:** 6

**Where:** `cmake/BagiEngine.cmake` (a target is Windows, Apple or else "Linux"; `SDL_SHARED ON`; `be_add_game_executable` bakes the checkout paths into `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` and copies the Vulkan loader), `CMakePresets.json` (macOS and Windows only; the release presets only turn asserts off), `src/Application/Application.cpp` (`Run()` is a blocking `while`), `src/Modules/TaskSystem/`, `Logger/FileSink.h` and `Control/ControlService` (threads and mutexes), `config/ApplicationConfig.xml` (`renderer="VulkanRendererBackend"`), `src/Modules/SDL/src/CoreSDL/SDLEventsProvider.cpp` (no touch or app-lifecycle events), `FileSystem/CLAUDE.md` (writes go to the resolved path in the checkout), `external/SDL/android-project` and the SDL Emscripten support (present, not used).

**What is missing** (found by reading the repository, not by building for these targets):

1. **PC:** no packaging. The content roots are the absolute paths of the developer's checkout and the assets are loose files, so a copied build runs only where the checkout is. There is no macOS `.app`, no Windows archive or installer, no version or icon. The Windows Vulkan DLL is git-ignored (see the CI entry). Saves and edited configs would be written into the checkout.
2. **Android:** no NDK toolchain and no Gradle project (SDL ships a template), and CMake would call an Android build "Linux". There are no touch events, no pause / resume handling and no per-app storage path, and the layout is fixed pixels.
3. **Web:** no Emscripten toolchain. `Run()` never gives control back to the browser, `TaskSystem`, `FileSink` and the control service use threads (browser threads need SharedArrayBuffer and special headers, or a single-thread build), SDL is built shared, the assets are loose files, and Vulkan does not run in a browser.
4. The control API's server is a development tool and must not ship in a release.

**A fix has to cover:**

- **backend first:** web has no Vulkan, so Vulkan cannot be the only release backend. Recommendation: the SDL renderer on all release targets (option (a) in the renderer entry); the SDL3 GPU option (c) must first be checked for a browser backend;
- a release configuration: Game mode, no ImGui or editor widgets, the control server off, content roots relative to the executable (`SDL_GetBasePath`), saves and user config in `SDL_GetPrefPath`, packed assets (the pak in the entry above);
- **PC** (Windows and macOS; Linux if wanted, with the presets from the test-gaps entry): a release preset, a `.app` bundle and a Windows folder or zip, a version, the Vulkan DLL question settled;
- **web:** an Emscripten preset, static SDL, a frame callback instead of the blocking loop, a decision on threads, a preloaded pak, an HTML shell;
- **Android:** an NDK preset on top of SDL's `android-project`, touch and lifecycle events in `SDLEventsProvider` (with the input mapping entry), safe-area and screen size handling (with the UI entry), a signed APK or AAB;
- one command (or one CI job per target) that produces the artifacts, and a smoke run per target;
- the order: PC first (packaging and relative roots), then web, then Android. Mahjong is the first game to release because it needs only mouse and touch.

## Test gaps and platforms

**Stage:** 5

**Where:** `tests/BECore/` (engine tests), `games/*/tests/`, `CMakePresets.json` (macOS and Windows presets only), `cmake/BagiEngine.cmake` (it has a Linux branch).

**What is missing** (found by reading the repository):

1. `Widgets` and most components have no tests (`Renderer` has the primitives and the view fit, with the `RecordingRenderer` double in `tests/Common`). `Scene` / `SceneManager` are covered only for which scene reacts to events (`SceneEventsTest`). The tests cover reflection, events, file system, resources, TaskSystem, format, enums and the pure game rules.
2. There are no Linux presets although the CMake has a Linux branch.

**A fix has to cover:**

- Linux presets (`linux-debug`, `linux-release`) and a CI job once the CI entry is done.

## Config and data code reads XmlNode by hand where the factory and reflection path already fits

**Stage:** 5

**Where:** `src/Modules/BECore/Application/` (`ApplicationFabric.cpp`, `IApplicationFabric.h`, `RunMode.cpp`), `BECore/Control/ControlSettings.cpp`, `src/Modules/SDL/src/CoreSDL/` (`SDL3ApplicationFabric.cpp`, `SDLMainWindow.cpp`), `src/Modules/Games/src/Games/GameManager.cpp`, `BECore/Resource/` (`TextureLibrary.cpp`, `ResourceManager.h`), `BECore/Widgets/WidgetManager.cpp`, `BECore/Config/ConfigFactory.h`, `src/Widgets/SceneEditor/SceneEditorWidget.cpp`, `src/Widgets/ConfigEditor/`, `games/Match3/src/Match3/Match3GameControl.cpp`, `games/Chess/src/Chess/ChessGameControl.cpp`.

**What is wrong** (found by reading the code, not by running anything): the engine is built on one split. A type name picks the class through `AbstractFactory`, and the reflected fields are filled through `IDeserializer`, so the model does not know the file format (`XmlDeserializer` and `BinaryDeserializer` already exist). Scene components, widgets, runtime tests, games, resource loaders, log sinks, assert handlers and the managers built with `MakeFromConfig` follow it. The places below read or write the format themselves instead:

1. `ApplicationConfig` has four hand-written readers, each pulling attributes by name: `ApplicationFabric::Create` (`type`), `SDL3ApplicationFabric::Create` (`path`, `renderer`), `ReadRunMode` (`mode`) and `ResolveControlSettings` (`control`, `controlPort`, `manualClock`, `headless`). `IApplicationFabric::Create` takes an `XmlNode`, so a factory-base interface knows the format. `ControlSettings` also has a second hand parser for the command-line flags, a merge rule (file first, flags override) and its own range checks.
2. `GameManager::Initialize` reads `defaultGame/@name` of `GamesConfig` through `XmlNode`.
3. `SpriteEntry` is not reflected: `TextureLibrary::Initialize` walks `<sprites>` with `XmlNode` and `Save` rebuilds the tree with raw pugixml.
4. `WidgetManager::CreateWidgets` has its own loop over `<widget type>` with its own factory call, while `TestManager` builds the same kind of list with `Ser::Load`. There are two ways to fill a factory list from a config.
5. `MakeFromConfig`, `TestManager`, `WidgetManager`, `SDLMainWindow` and `ResourceManager::LoadSerialized` / `LoadSerializedAsync` each construct an `XmlDeserializer` and call `LoadFromXmlNode`. `ConfigManager::GetConfig` returns an `XmlNode`, never an `IDeserializer`. `LoadSerialized` is XML-only although loaders are otherwise picked by file extension and `BinaryDeserializer` exists. `SDLMainWindow` also opens its own `XmlConfig` by path.
6. A source that is not a file has no path to a reflected type. The start-up flags, the parameters of the control API and the planned CSS declarations of the UI plan (`.claude/plans/magical-leaping-blossom.md`, task 4.2) all mean "values for named fields of a typed object", and each is parsed and range-checked on its own. `Match3GameControl` and `ChessGameControl` build the state and the actions as `Json` objects by hand and read the parameters of `PerformAction`, `Reset` and `SetState` member by member through `Control/Json.h`. There is no `ISerializer` / `IDeserializer` over JSON or over a name → value map.
7. `SceneEditorWidget::SaveScene` and `BuildNodeXml` write the `<scene>` attributes and the `name` / `layer` of a `<node>` by hand, so a new reflected field of `Scene` or `SceneNode` is dropped on save. The scene part is also in the entry "The scene editor saves only the active scene"; the node part is not.
8. `ConfigEditorWidget` edits XML attributes (`StagedChanges::ApplyTo` on the pugixml node) and keeps its field hints under XML paths (`ConfigFieldSchema`, `"sink/@type"`), guessing the type from the text (`"true"` is a bool, a number that parses is an int). The scene inspector already edits through `IPropertyVisitor` with the real types. The entry "Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve" is a symptom of this.
9. The examples in the comments of `Reflection/TypeTraits.h` and `Reflection/ReflectionMarkers.h` still show `BE_FUNCTION bool Initialize(const XmlNode& node)`; the hook the code uses is `Initialize(IDeserializer&)`.

**A fix has to cover** (goals, not a design):

- one way to get an `IDeserializer` for a named config, so a caller does not pick `XmlDeserializer` itself, and one way to fill a factory list from a config (items 4 and 5);
- the application, game and control settings as reflected structs, with the file and the command line as two sources of the same struct (items 1 and 2);
- `TextureLibrary` entries reflected and saved through `ISerializer` (item 3);
- a source that is not a file: a name → value map and a JSON-backed pair, shared by the start-up flags, the control API and the UI plan (item 6); the JSON side keeps the rules of `Control/Json.h` (nlohmann aborts on a bad access in this build);
- both editors writing through the reflection (items 7 and 8); the two existing entries stay the place for their bugs;
- an unknown type is reported: the generic `Ser::Load` path drops it silently (the first entry of this file), so item 4 must not lose the error `WidgetManager` logs today;
- the comments of item 9 corrected and `BECore/Config/CLAUDE.md` updated with the rule.

## The reflection generator is regex-based

**Stage:** 5

**Where:** `CI/meta_generator/` (`core/parser.py`, `core/generator.py`, about 2000 lines of Python), `CI/meta_generator/reflection_targets.json`.

**What is wrong** (found by reading the code and the first TODO entry, not by fuzzing the parser): the parser matches `BE_CLASS` / `BE_REFLECT_FIELD` with regular expressions, so unusual syntax (macros in declarations, nested templates, attributes) can be missed or misparsed without an error. The duplicate `LogSinkType` in the first entry is one example of generated and handwritten code disagreeing without a warning.

**A fix has to cover** (not urgent while the generator copes):

- a warning when the generator skips a marker it could not parse;
- a regression suite of tricky declarations for the parser;
- an evaluation of libclang instead of regular expressions, only if the first two do not hold.
