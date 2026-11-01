# TODO

Known problems that are not being worked on yet. One entry per problem: where it is, what is wrong, what a fix has to cover. A finished entry is deleted, not marked done.

**Entry fields.** `tools/todo_sync/` reads this file to build a progress dashboard and to export the entries to a task tracker (its `CLAUDE.md` has the commands). The entry id is the slug of its heading, so renaming a heading makes a new entry for the tracker. Every field below is optional and sits on its own line near the top of the entry:

- `**Status:**` starts with one keyword, `open` (the default), `in progress` or `deferred`, then a date `YYYY-MM-DD` and free text;
- `**Stage:**` the roadmap number (1 to 6); a problem outside the roadmap order has none;
- `**Plan:**` the path of a plan under `.claude/plans/`; its `- [ ]` / `- [x]` lines give the progress.

## Roadmap

Direction for the engine, not a problem entry. The engine has strong infrastructure (reflection, layered config, TaskSystem, ImGui editor, tests) and no runtime for a real 2D game: no animation, text, audio or input mapping. The entries below follow this order. Every "missing" statement was found by reading the code, not by running a game.

1. **Hygiene:** CI is deferred by the project owner (entry below; the Linux job in the test-gaps entry waits for it). The link from this file to a task tracker and a dashboard is done: `tools/todo_sync/`.
2. **External control API (in progress):** a model or a program plays a game over MCP / WebSocket (entry below).
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
- a guard on the number of tests (284 at the last count): it must not drop;
- a Linux job once Linux presets exist (see the entry on test gaps and platforms).

## No external control: a game can be played only by a human at the window

**Status:** in progress since 2026-11-01. Stages 0 to 3 are done: the spikes, the command queue, the game interface with the Match3 adapter and the WebSocket server with its start-up flags (`BECore/Control/CLAUDE.md`, `Games/CLAUDE.md`, `games/Match3/CLAUDE.md`, `RemoteControl/CLAUDE.md`; `BagiMatch3 --control` plays over `ws://127.0.0.1:8765`). Chess has its full rules, FEN, UCI and a model-level move (`games/Chess/CLAUDE.md`). Next: manual clock and step, headless and screenshot, then the Chess adapter and the Stockfish and MCP bridges.

**Stage:** 2

**Plan:** `.claude/plans/buzzing-tinkering-cat.md`

**Spike results** (macOS, AppleClang 17, SDL 3.2.0; Windows and MSVC not checked yet):

- Headless: `SDL_VIDEO_DRIVER=dummy` with the software renderer works (pixels correct, PNG through `IMG_SavePNG_IO` into memory, byte-identical to the Metal result). A hidden window on the `cocoa` driver with the software renderer does **not** (`software not available`); with Metal it works. The `offscreen` driver needs OpenGL / GLES. So `--headless` means the SDL backend on the `dummy` video driver, no window at all.
- nlohmann/json with `JSON_NOEXCEPTION`: `at()`, `get<T>()` on the wrong type, `value()` on a non-object and const `operator[]` by a missing key abort the process. Client JSON is read only through the checked helpers of `BECore/Control/Json.h`.

**Where:** `src/Application/Application.cpp` (`Run()` is a blocking loop on the real clock, input comes only from the SDL window), `games/Chess/src/Chess/Game/ChessGameModel.h` (`TryMove(Move)` plays a move, `OnSquareClicked(Square)` imitates the mouse; neither can be reached from outside the process), `src/Modules/RemoteControl/` (the WebSocket server, built and tested on macOS only).

**What is missing** (found by reading the code and searching `src`, `games` and CMake, not by running a game):

1. No MCP server: the WebSocket server exists, but a model cannot reach it as tools yet. Windows is unchecked: Winsock, `ws2_32` and the `WinMain` arguments have not been built or run there.
2. Chess has no adapter that exposes its state and actions to a program. The pieces exist in the model (`MoveGenerator::GenerateLegal`, `ChessGameModel::TryMove`, `Position::ToFen`, UCI through `MoveGenerator::ParseUci`), but nothing outside the process can reach them and the model cannot be set to a given position.
3. No run without a person: the window and the renderer are always created (the Game run mode removed the editor, not the window). The loop ticks the real clock and has no "advance N frames" step; `Time::Advance(seconds)` exists, but only tests call it.
4. No observation beyond the window: `IRenderer` cannot read pixels back, so there is no screenshot. The scene state can be dumped through `ISerializer`.

**A fix has to cover** (the goal: a whole game can be developed, played and tested by a model or by an external program with no human at the window; Chess against Stockfish is the acceptance example):

- the game-side interface (`IControllableGame`) implemented by Chess, as it is for Match3; the `step N frames` and `screenshot` commands on the existing control service;
- the `RemoteControl` tests and the smoke test built and run on Windows (the server is done and tested on macOS);
- the state-change push (`state_changed`): the server already sends what `ControlService::Publish()` gives it, nothing publishes yet;
- an MCP server so Claude can play and debug a game. Recommendation: a thin separate process in `tools/` that maps MCP tools (`get_state`, `perform_action`, `step`, `screenshot`, `reset`) onto the WebSocket, so the engine has no MCP dependency;
- a headless mode on top of the Game run mode (`RunMode`, `BECore/CLAUDE.md`): a hidden or absent window, offscreen rendering for screenshots, an optional manual clock (`Time::Advance`) for deterministic runs; a screenshot needs a pixel readback in `IRenderer` and in both backends;
- a Stockfish bridge as an external script in `tools/`: it reads the position over the WebSocket, asks Stockfish over UCI and sends the move back, so Stockfish is never linked into the engine;
- tests: the smoke test (`BagiMatch3ControlSmoke`) runs the game headless instead of in a window, a Chess scenario joins it, and a Chess ↔ Stockfish game as an optional check that is skipped when Stockfish is not installed.

## Third game: Mahjong solitaire

**Stage:** 6

**Where:** `games/Chess` and `games/Match3` (the models to follow), `cmake/BagiEngine.cmake` (`be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests`), `src/Modules/Games/CLAUDE.md` (the engine-game contract), `src/Modules/Games/src/Games/IControllableGame.h`.

**What is missing:** the game. Assumed: tile-matching solitaire (Shanghai), one player. The four-player rules game needs AI players and is out of scope. It is the consumer that the roadmap "Driver" asks for.

**A fix has to cover:**

- `games/Mahjong/` built like the other two: an `IGame` registered in `AbstractFactory<IGame>`, a rules model with no rendering and no animation, an executable `BagiMahjong`, `MahjongTests`; no engine code names the game;
- the rules as pure code with gtests: layouts of stacked layers, the free-tile rule (nothing on top, and the left or right side open), pair matching (flowers and seasons match within their group), a deal that is always solvable (generated by playing backwards), shuffle when no move is left, hint, undo, a seeded platform-independent RNG like Match3's;
- a control API adapter (`IControllableGame`): the state and the list of free pairs as actions, so a model can play and test it;
- stacked tiles use the node `layer` that the draw pass already sorts by (`SceneNode::SetLayer` at runtime, `Scene/CLAUDE.md`, "Draw order"): one node per tile, the layer from its height in the layout. No renderer work is needed first;
- the features it consumes: animation, text, audio, input mapping with touch, in-game UI, save game (resume a deal);
- the first game to go through the release entry on all three targets.

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
2. No rotation or scale of a sprite, no flip, no tint or alpha beyond the rect color.
3. No movable 2D camera: a scene can fit a fixed view size to the output (`viewWidth` / `viewHeight`, `Scene/CLAUDE.md`, "View size"), but scrolling and zoom need a camera transform on top of that.
4. No blend modes.
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
- where the HUD goes in a mode that has ImGui: its window does not move with the fitted board.

## No text rendering in the engine: text exists only in ImGui

**Stage:** 3

**Where:** `games/Match3/src/Match3/Components/Match3BoardComponent.cpp` (the score is `ImGui::Text` in an ImGui window), `src/Modules/BECore/Resource/` (where a font loader would go), `external/` (SDL_image is the only SDL add-on).

**What is missing** (found by reading the code, not by running a game): fonts, glyph rendering and a text component. The Match3 HUD is ImGui, and the Game run mode has no ImGui, so `BagiMatch3` shows no score and has no Reset button until the HUD moves to a text component.

**A fix has to cover:**

- a font loader as an `IResourceLoader` (SDL3_ttf, or a glyph atlas built at load time) following `Resource/CLAUDE.md`;
- a `TextComponent` (text, font, size, color, alignment) drawn through `IRenderer`, so it works in both backends;
- the Match3 score moved from ImGui to the text component.

## No audio

**Stage:** 3

**Where:** no module, no dependency (`external/` has SDL, SDL_image, Vulkan headers and loader, googletest).

**What is missing** (found by searching `src`, `games` and CMake): sound effects and music.

**A fix has to cover:**

- a library choice (SDL3_mixer or miniaudio) and a new module placed in the module order of `CLAUDE.md` without a circular dependency;
- `AudioClip` as a resource through `IResourceLoader`, an `AudioSource` component and playback events (so games trigger sounds without including the library);
- volume and mute in config; tests run without an audio device (a null or dummy driver).

## Input has no mapping layer: actions are just names

**Stage:** 3

**Where:** `src/Modules/BECore/Input/ActionsLibrary.h` (a set of action names), `src/Modules/Events/src/Events/InputEvents.h` (`ActionEvent`), `src/Modules/SDL/src/CoreSDL/SDLEventsProvider.cpp` (`ConvertSDLEvent`), `SDLEvents.h` (key and mouse event types, no gamepad events), `Scene/Components/ClickableComponent.cpp`.

**What is missing** (found by reading the code, not by running a game):

1. `ConvertSDLEvent` forwards only `QuitEvent` and `MouseButtonDownEvent` (and the latter only when ImGui does not want the mouse). The conversions for keys, mouse-up, motion, wheel and window events sit in a commented-out block, so `KeyDownEvent`, `MouseMotionEvent` and the others are declared but never emitted. Raw events still reach subscribers of `SDLEvents::SDLEventWrapper`.
2. There is no binding from physical input (key, mouse button, gamepad button or axis) to an action. `ActionsLibrary` only lists names, `ClickableComponent` emits the action itself, and keys and gamepads have no path to `ActionEvent`.

**A fix has to cover:**

- the SDL conversions restored or rewritten so key, mouse and window events are emitted again, with a rule for when ImGui keeps the input;
- an `InputMap` read from XML config (layered, so a game ships its own): key / button / axis → action, with pressed, held and released states and an axis value;
- gamepad support through SDL3 events;
- components that react to mapped actions or raw input subscribe with `SubscribeInScene`, so only the active scene reacts (`ClickableComponent` already does);
- a gtest that feeds raw events and checks the resulting actions.

## No in-game UI: UI exists only as ImGui editor panels

**Stage:** 3

**Where:** `src/Widgets/` (editor widgets), `Scene/Components/ClickableComponent.cpp` (the only clickable thing on a scene).

**What is missing** (found by reading the code, not by running a game): buttons, panels, labels and layout that belong to a game and render through `IRenderer`. Do this after text rendering and input mapping.

**A fix has to cover:**

- a few scene components (panel, label, button with normal / hover / pressed state) on top of `TransformComponent`, `TextComponent` and the input map;
- anchoring to the window or viewport size;
- a main-menu scene in one of the games as the first consumer.

## Game UI has no LLM-friendly authoring path yet: choose one before building it

**Stage:** 3

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

1. `Renderer`, `Widgets` and most components have no tests. `Scene` / `SceneManager` are covered only for which scene reacts to events (`SceneEventsTest`). The tests cover reflection, events, file system, resources, TaskSystem, format, enums and the pure game rules.
2. There are no Linux presets although the CMake has a Linux branch.

**A fix has to cover:**

- a renderer test double (an `IRenderer` that records calls), so components can be tested without a window;
- Linux presets (`linux-debug`, `linux-release`) and a CI job once the CI entry is done.

## The reflection generator is regex-based

**Stage:** 5

**Where:** `CI/meta_generator/` (`core/parser.py`, `core/generator.py`, about 2000 lines of Python), `CI/meta_generator/reflection_targets.json`.

**What is wrong** (found by reading the code and the first TODO entry, not by fuzzing the parser): the parser matches `BE_CLASS` / `BE_REFLECT_FIELD` with regular expressions, so unusual syntax (macros in declarations, nested templates, attributes) can be missed or misparsed without an error. The duplicate `LogSinkType` in the first entry is one example of generated and handwritten code disagreeing without a warning.

**A fix has to cover** (not urgent while the generator copes):

- a warning when the generator skips a marker it could not parse;
- a regression suite of tricky declarations for the parser;
- an evaluation of libclang instead of regular expressions, only if the first two do not hold.
