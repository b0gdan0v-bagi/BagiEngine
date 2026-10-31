# TODO

Known problems that are not being worked on yet. One entry per problem: where it is, what is wrong, what a fix has to cover.

## Roadmap

Direction for the engine, not a problem entry. The engine has strong infrastructure (reflection, layered config, TaskSystem, ImGui editor, tests) and no runtime for a real 2D game: no frame time, no animation, text, audio or input mapping, and the game cannot run without the editor. The entries below follow this order. Every "missing" statement was found by reading the code, not by running a game.

1. **Hygiene:** CI is deferred by the project owner (entry below; the Linux job in the test-gaps entry waits for it). Scene-scoped events are done: components subscribe with `SubscribeInScene` (`BECore/Scene/CLAUDE.md`).
2. **Time and run modes:** frame time, game-only run mode.
3. **Minimum for a 2D game, in this order:** animation, 2D rendering, text, audio, input mapping, in-game UI.
4. **Owner decisions:** renderer backends, what ECS means in this engine.
5. **Content and quality:** prefabs, saves / hot-reload / packing, test gaps and platforms, the reflection generator.

**Not now:** networking, 3D, a physics engine of our own (take Box2D when physics is needed), a scripting language, a rewrite to pure ECS (reflection, the inspector and serialization are built around `IComponent`), mobile and web.

**Driver:** build a third small game (arcade or platformer) so that each feature in stage 3 has a concrete consumer (movement with `dt`, camera, animation, sound, score text, gamepad), and make Match3 animated as the regression check. Stages 1 to 3 are done when `BagiMatch3` runs without the editor, gems fall smoothly, the score is drawn with an engine font, a match plays a sound, input goes through an input map, and CI builds and tests it on macOS and Windows (CI is deferred, so that last point waits for its entry).

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

**Where:** the repository has no `.github/` and no other pipeline definition. Presets are in `CMakePresets.json`, tests run through `ctest --preset ...`.

**What is missing** (found by reading the repository, not by running anything):

1. Nothing builds or tests a push. The engine supports macOS and Windows, so a break on the other OS goes unnoticed until someone builds there.
2. The Vulkan loader is vendored only in part: `libvulkan.1.dylib` (macOS) and `vulkan-1.lib` (Windows) are tracked, the `*.dll` files are git-ignored and have to be provided by hand (`external/Vulkan-Loader/README.md`). A CI runner on Windows needs an answer for the runtime DLL, if the test and game executables load it.

**A fix has to cover:**

- a workflow for macOS (`macos-debug`) and Windows (one MSVC or Clang preset) that checks out the submodules (`git submodule update --init --recursive`, `external/googletest` included), configures, builds the game and test targets and runs `ctest`;
- a decision for the Vulkan loader DLL on the Windows runner (install the SDK, fetch the loader, or build without the Vulkan backend), after checking whether the test executables need it at all;
- a guard on the number of tests (152 at the last count): it must not drop;
- a Linux job once Linux presets exist (see the entry on test gaps and platforms).

## No frame time: update has no `deltaTime` and no fixed step

**Where:** `src/Modules/BECore/Scene/Scene.cpp` (`Scene::Update` emits `SceneEvents::SceneUpdateEvent::Emit()` with no payload), `GameManager/CoreManager.cpp` (`OnGameCycle`), `src/Application/Application.cpp` (`Run` loop).

**What is missing** (found by reading the code, not by running a game):

1. Nothing measures frame time. A search of `BECore`, `Application`, `SDL`, `Widgets` and the games finds a clock only in the logger sinks (timestamps), so movement and animation cannot be frame-rate independent.
2. `SceneUpdateEvent` carries no data, and there is no fixed-step update for game logic.

**A fix has to cover:**

- a `Time` service (`Singleton`) updated once per frame in `Application::Run`, with `deltaTime`, total time and a frame counter;
- `dt` available to components (in `SceneUpdateEvent` or through `Time`), and a fixed-step update for logic that must be deterministic;
- the stage order of a frame (input → logic → render) written down in `BECore/CLAUDE.md`;
- a gtest that drives a component with a fake clock and shows its result does not depend on the frame rate.

## No game-only run mode: the game exists only inside the editor viewport

**Where:** `config/WidgetsConfig.xml` (always creates `EditorLayoutWidget`, `ViewportWidget` and the editors), `src/Widgets/Viewport/ViewportWidget.cpp` (the only caller of `SceneManager::DrawAll()`), the comment at the end of `CoreManager::OnGameCycle`.

**What is wrong** (found by reading the code, not by running a game):

1. The scene is drawn only by `ViewportWidget`: it renders into an offscreen target and shows it with `ImGui::Image`. A game can replace `WidgetsConfig.xml` (content layering), but without `ViewportWidget` nothing draws the scene.
2. Mouse clicks reach `ClickableComponent` and the game components in scene space only because `ViewportWidget` re-emits `MouseButtonDownEvent` with viewport-local coordinates (`SDLEventsProvider` forwards the raw event, in window coordinates, only when ImGui does not want the mouse). Only mouse-button-down exists on both paths, for the left, right and middle buttons.
3. So every game executable ships with the editor, and a game cannot be run or shown as a product.

**A fix has to cover:**

- an Editor / Game mode chosen by config (the game's own `WidgetsConfig.xml` or a field in `ApplicationConfig.xml`), the editor behavior unchanged;
- in Game mode the scene is drawn straight into the back buffer by a runtime owner instead of `ViewportWidget`, and window input goes to the scene without the viewport remapping;
- `BagiChess` and `BagiMatch3` start in Game mode without ImGui panels (the Match3 HUD is ImGui today, see the text entry).

## No animation: no tweens, easing or timelines

**Where:** `games/Match3/CLAUDE.md` ("`Match3GameModel` never animates"), `games/Match3/src/Match3/Components/Match3BoardComponent.cpp`, `src/Modules/TaskSystem/` (coroutines that animation could use).

**What is missing** (found by reading the code, not by running a game): no way to change a property over time. Match3 resolves a whole cascade instantly, and Chess pieces jump to the target square. Depends on the frame time entry.

**A fix has to cover:**

- a tween API for float, vector and color properties with easing functions, cancel and completion callbacks, ticked by `dt`;
- use from coroutines (`co_await` a tween) through the TaskSystem on the main thread;
- Match3 as the first consumer (swap, fall and refill animation) while the model stays animation-free (the rule in `games/Match3/CLAUDE.md`);
- gtests for the easing math and for tween ordering with a fake clock.

## 2D rendering is minimal: no layers, rotation, camera or blending

**Where:** `src/Modules/BECore/Renderer/IRenderer.h` (`DrawFilledRect`, `DrawTexture` are the whole drawing surface), `SDLRendererBackend.cpp`, `Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp`, `Scene/Components/TransformComponent.h` (position and size in screen pixels).

**What is missing** (found by reading the code, not by running a game):

1. No z-order or layers: draw order is the order of event handlers.
2. No rotation or scale of a sprite, no flip, no tint or alpha beyond the rect color.
3. No 2D camera: positions are screen pixels, so scrolling and zoom need a camera transform.
4. No blend modes.
5. A node has only absolute pixel coordinates, no parent-relative transform.

**A fix has to cover:**

- each capability first in `IRenderer` / `TransformComponent`, then in both backends (`.claude/rules/rendering-module.md`; a backend that cannot do it documents the fallback);
- a layer (sort key) on renderer components with a stable order;
- a camera that maps world to screen, used by the renderer components;
- a decision on the backends before the work starts (see the entry on renderer backends).

## No text rendering in the engine: text exists only in ImGui

**Where:** `games/Match3/src/Match3/Components/Match3BoardComponent.cpp` (the score is `ImGui::Text` in an ImGui window), `src/Modules/BECore/Resource/` (where a font loader would go), `external/` (SDL_image is the only SDL add-on).

**What is missing** (found by reading the code, not by running a game): fonts, glyph rendering and a text component. This ties the Match3 HUD to ImGui, which is an obstacle for the game-only run mode.

**A fix has to cover:**

- a font loader as an `IResourceLoader` (SDL3_ttf, or a glyph atlas built at load time) following `Resource/CLAUDE.md`;
- a `TextComponent` (text, font, size, color, alignment) drawn through `IRenderer`, so it works in both backends;
- the Match3 score moved from ImGui to the text component.

## No audio

**Where:** no module, no dependency (`external/` has SDL, SDL_image, Vulkan headers and loader, googletest).

**What is missing** (found by searching `src`, `games` and CMake): sound effects and music.

**A fix has to cover:**

- a library choice (SDL3_mixer or miniaudio) and a new module placed in the module order of `CLAUDE.md` without a circular dependency;
- `AudioClip` as a resource through `IResourceLoader`, an `AudioSource` component and playback events (so games trigger sounds without including the library);
- volume and mute in config; tests run without an audio device (a null or dummy driver).

## Input has no mapping layer: actions are just names

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

**Where:** `src/Widgets/` (editor widgets), `Scene/Components/ClickableComponent.cpp` (the only clickable thing on a scene).

**What is missing** (found by reading the code, not by running a game): buttons, panels, labels and layout that belong to a game and render through `IRenderer`. Do this after text rendering and input mapping.

**A fix has to cover:**

- a few scene components (panel, label, button with normal / hover / pressed state) on top of `TransformComponent`, `TextComponent` and the input map;
- anchoring to the window or viewport size;
- a main-menu scene in one of the games as the first consumer.

## Renderer backends: one decision, then batching

**Where:** `src/Modules/BECore/Renderer/IRenderer.h`, `src/Modules/SDL/`, `src/Modules/Vulkan/`, `external/Vulkan-Loader/` (vendored loader for macOS, which runs Vulkan on top of Metal).

**What is wrong** (found by reading the code and docs):

1. Every new `IRenderer` capability has to be written twice, in `SDLRendererBackend` and `VulkanRendererBackend` (`rendering-module.md`). For a 2D engine this doubles the cost of every item in the 2D rendering entry.
2. A draw is one `IRenderer` call per object; there is no batching, atlas support or sort pass in the interface.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- decide which of: (a) the SDL renderer is the main backend and Vulkan stays frozen at its current feature level, (b) keep both and implement new features in one backend first, (c) replace both with the SDL3 GPU API, which gives native Metal on macOS instead of the loader. Recommendation: a 1-2 day spike on (c), and (a) until it is decided;
- after the decision, sprite batching and texture atlases (`TextureLibrary` already maps a sprite name to a texture and a source rect);
- record the decision in `rendering-module.md` and `Vulkan/CLAUDE.md`.

## ECS is nominal: EnTT only hands out entity ids

**Where:** `src/Modules/BECore/Scene/Scene.h` (`entt::registry _registry`), `SceneNode.cpp` (`_entity = scene->GetRegistry().create()`), `Scene/IComponent.h`, `Scene/CLAUDE.md`, `CLAUDE.md` ("modular ECS architecture").

**What is wrong** (found by reading the code):

1. The registry holds one empty entity per `SceneNode`. Components are polymorphic `IComponent` objects stored in the node; there are no registry views, systems or component storage. The architecture description in `CLAUDE.md` promises more than the code does.
2. Rendering is a per-component event handler on `SceneDrawEvent`, so there is nowhere to sort or batch draws.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- recommendation: keep the object model (`SceneNode` + `IComponent` fits reflection and the inspector) and make rendering a system that collects `TransformComponent` + renderer data from the registry, sorts by layer and hands a batch to `IRenderer`. This gives EnTT a real job and prepares the 2D rendering and batching entries;
- the alternative is to say plainly that this is a scene graph in the Unity style and drop EnTT if ids are all it provides;
- not a rewrite to pure ECS;
- record the decision in `Scene/CLAUDE.md` and correct the wording in `CLAUDE.md`.

## No prefabs: all of a game's scenes live in one `SceneConfig.xml`

**Where:** `games/*/config/SceneConfig.xml`, `Scene/CLAUDE.md` ("Every `SceneConfig.xml` replaces the one below it"), `Scene/SceneManager.cpp`.

**What is missing** (found by reading the docs and configs): a reusable node template and a scene in its own file. A game's `SceneConfig.xml` replaces the engine's whole file, and a node such as a Match3 gem or a Chess piece is spelled out every time it appears.

**A fix has to cover:**

- a prefab: a node (with children and components) in its own XML file, instantiated by name with per-instance overrides, and the same instantiation at runtime (`Scene::CreateNode` equivalent);
- scenes loaded from separate files listed in `SceneConfig.xml`, keeping the layering rule (a game's file wins);
- the scene editor saving prefab instances without flattening them.

## No save games, hot-reload or asset packing

**Where:** `src/Modules/BECore/Reflection/BinarySerializer.h` / `BinaryDeserializer.h` (format in `.claude/rules/reflection-module.md`), `src/Modules/BECore/FileSystem/`, `Resource/`.

**What is missing** (found by reading the code, not by running a game):

1. `BinarySerializer` has no consumer: a game cannot save and restore its state.
2. Changing a texture or config needs a restart; there is no file watcher.
3. Assets are loose files, with no packed format for a release build.

**A fix has to cover:**

- a save game service built on `BinarySerializer` (versioned, name-keyed as the format already is) with a Chess or Match3 model as the first consumer;
- hot-reload of textures and configs in Editor mode through `FileSystem` and `ConfigManager::ReloadConfig`;
- later: a pak file behind the virtual `FileSystem` mount points, so a release reads packed assets through the same paths.

## Test gaps and platforms

**Where:** `tests/BECore/` (engine tests), `games/*/tests/`, `CMakePresets.json` (macOS and Windows presets only), `cmake/BagiEngine.cmake` (it has a Linux branch).

**What is missing** (found by reading the repository):

1. `Renderer`, `Widgets` and most components have no tests. `Scene` / `SceneManager` are covered only for which scene reacts to events (`SceneEventsTest`). The tests cover reflection, events, file system, resources, TaskSystem, format, enums and the pure game rules.
2. There are no Linux presets although the CMake has a Linux branch.

**A fix has to cover:**

- a renderer test double (an `IRenderer` that records calls), so components can be tested without a window;
- Linux presets (`linux-debug`, `linux-release`) and a CI job once the CI entry is done.

## The reflection generator is regex-based

**Where:** `CI/meta_generator/` (`core/parser.py`, `core/generator.py`, about 2000 lines of Python), `CI/meta_generator/reflection_targets.json`.

**What is wrong** (found by reading the code and the first TODO entry, not by fuzzing the parser): the parser matches `BE_CLASS` / `BE_REFLECT_FIELD` with regular expressions, so unusual syntax (macros in declarations, nested templates, attributes) can be missed or misparsed without an error. The duplicate `LogSinkType` in the first entry is one example of generated and handwritten code disagreeing without a warning.

**A fix has to cover** (not urgent while the generator copes):

- a warning when the generator skips a marker it could not parse;
- a regression suite of tricky declarations for the parser;
- an evaluation of libclang instead of regular expressions, only if the first two do not hold.
