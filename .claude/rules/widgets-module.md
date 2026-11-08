---
paths:
  - "src/Widgets/**/*"
  - "src/Modules/BECore/Widgets/**/*"
---

# Widget System Guidelines

## Overview

Widgets are the engine's per-frame building blocks implementing `IWidget` (a `FACTORY_BASE` that also derives `SubscriptionHolder`). `WidgetManager` creates them from XML (the reflected holder `WidgetList`, filled by `MakeFromConfig`) and drives them every frame. There are two kinds:

- **Editor panels**: `SceneEditorWidget`, `ConfigEditorWidget`, `TextureEditorWidget`, `EditorLayoutWidget`, `ViewportWidget`. They build ImGui windows. `ViewportWidget` also has aspect presets and a safe-area simulation for looking at a UI layout in several shapes (`src/Modules/BECore/UI/CLAUDE.md`, "Editor preview").
- **Frame widgets**: steps of the render frame that are not UI. `ClearScreenWidget` clears the back buffer and opens the swapchain pass, `SceneRenderWidget` draws the scene (Game mode), `ImGuiWidget` begins and renders the ImGui frame (Editor mode).

The editor panels exist only in a build with `BE_WITH_EDITOR` (Debug always; Release only when switched on: root `CLAUDE.md`, "Build Commands"); without it `src/CMakeLists.txt` leaves the five folders `ConfigEditor`, `EditorLayout`, `SceneEditor`, `TextureEditor` and `Viewport` out of `RuntimeModule`, and `tests/CMakeLists.txt` leaves out the tests of the panels (`ConfigEditorSaveTest`, `SceneEditorSaveTest`, `ConfigFieldSchemaTest`). A new editor panel goes into one of these folders (or a new one added to the regex in `src/CMakeLists.txt`); code outside them that needs an editor header is guarded with `#if BE_WITH_EDITOR`. The frame widgets and `ImGuiWidget` are in every build, because a game may list `ImGuiWidget` in its own `GameWidgetsConfig.xml` for a HUD.

A widget is not game UI. In-game UI is a scene feature, canvases of `UICanvasComponent` (`src/Modules/BECore/UI/CLAUDE.md`); do not build it on `IWidget`.

## IWidget Interface

All methods are `BE_FUNCTION`.

| Method | Purpose |
|--------|---------|
| `Initialize(IDeserializer&)` | Called by the loader after the widget's reflected fields are read, inside its own `<widget>` scope (read an attribute or child that is not a field here, subscribe to events); return `false` on failure (the widget is then logged and not registered). Do not call `Deserialize(deserializer)` here: the loader has done it |
| `Update()` | Every frame. An ImGui panel builds its window here (`ImGui::Begin` ... `End`), between `ImGui::NewFrame` and `ImGuiWidget::Draw`. A frame widget issues its render commands here when they must come first (`ClearScreenWidget`) |
| `DrawScreen()` | Optional, default is empty. Draws into the back buffer after every `Update()` (`SceneRenderWidget` draws the scene here) |
| `Draw()` | Every frame, after `DrawScreen()`. Normally empty; `ImGuiWidget::Draw()` runs `ImGui::Render()` and the ImGui draw event |

## Widget Lifecycle

1. `WidgetManager::CreateWidgets()` (from `CoreManager::OnApplicationInit`) reads the run mode (`mode` in `ApplicationConfig.xml`, see `BECore/CLAUDE.md`) and fills a `WidgetList` with `MakeFromConfig<WidgetList>(configName)`: each `<widget type>` is created through `AbstractFactory<IWidget>` with the default constructor and its reflected fields are read. A missing config is logged (`WidgetManager: config '...' not found`) and no widget is created.
2. `Initialize(deserializer)`: subscribe to events, read what is not a field. An unknown `type` or an `Initialize` that returns `false` is logged once as `Config '<name>': widgets[N]...` (`Unknown factory type: 'X'` / `<Type> failed to initialize`) and the widget is skipped. A `<widget>` without `type` is skipped silently, and `enabled="false"` is honored.
3. Every frame, from `CoreManager::OnGameCycle`: `UpdateAll()` → (queued events) → `DrawScreenAll()` → `DrawAll()`, in registration order.
4. Destruction: `SubscriptionHolder` drops the event subscriptions. A widget can die in the middle of the run, not only at program end: in a build with the editor `WidgetManager::ApplyPendingMode()` replaces the widgets of one run mode by those of the other (Ctrl+Shift+E, applied at the end of a frame after `WaitIdle`; the order and what is lost: `BECore/CLAUDE.md`, "Run mode"). At program end `Application::Cleanup` calls `WidgetManager::DestroyAll()` before `ApplicationCleanUpEvent`. Both destroy the widgets **last created first**, with the GPU idle, and the renderer and its device are alive. So a destructor must give back what the widget set globally and release its GPU objects:
   - `ImGuiWidget::~ImGuiWidget()` calls `Destroy()` (idempotent through `_isInitialized`; the `ApplicationCleanUpEvent` subscription goes with the widget): it waits for the GPU, emits `ImGuiShutdownEvent` (the Vulkan backend frees ImGui's descriptor pool and pipeline there) and shuts the ImGui backends down.
   - `ViewportWidget::~ViewportWidget()` clears the safe-area override of `CoreManager::GetUIEnvironment()` (so a canvas does not keep the simulated safe area in Game mode) and releases its render target, whose destructor waits for the GPU and needs the ImGui context, which is why `ImGuiWidget` must come first in `WidgetsConfig.xml` (it dies last).
   - A new widget that sets something on `CoreManager` or in the scene resets it in its destructor. A mouse button pressed in the viewport and not released at a switch is lost (no button-up reaches the scene).

## XML Configuration

File: `config/WidgetsConfig.xml` (Editor mode) or `config/GameWidgetsConfig.xml` (Game mode: `ClearScreenWidget` + `SceneRenderWidget`, no ImGui, so a Game-mode widget must not touch ImGui). Entries `<widget type="..."/>` under `<widgets>`:
- `type`: widget class name (`ImGuiWidget`, `EditorLayoutWidget`, `ViewportWidget`, `SceneRenderWidget`, ...)
- widget-specific settings as `BE_REFLECT_FIELD` fields (for example `<clearColor r g b a/>` of `ClearScreenWidget`), filled by the loader; anything else is read in `Initialize`
- `enabled="false"` skips the entry
- the file order is the creation and update order; there is no `name` or `priority` attribute

The order is a contract, nothing checks it:
- `ImGuiWidget` is first in `WidgetsConfig.xml`: it creates the ImGui context, and every panel after it needs one.
- `ClearScreenWidget` comes before `SceneRenderWidget` in `GameWidgetsConfig.xml`: its `Update()` opens the swapchain pass that `SceneRenderWidget::DrawScreen()` records into.

## Best Practices

- **Single Responsibility**: one widget, one purpose.
- **XML Configuration**: tunable parameters come from the widget's XML node.
- **Event-Driven**: communicate through events, not direct widget-to-widget calls.
- **Lazy Initialization**: set up resources in `Initialize()`, not in the constructor.
- **ImGui in `Update()`**: a panel calls `ImGui::Begin()` / `ImGui::End()` inside `Update()` and leaves `Draw()` empty. `ImGuiWidget::Draw()` renders the ImGui frame first in `DrawAll()`, so a window built in a later widget's `Draw()` would miss the frame.
- A widget that runs in Game mode must not touch ImGui (there is no context).

## Scene Editor Conventions

Scene editor code is split across focused layers:

- `SceneEditorWidget`: orchestration (selection flow, scene-level actions, window structure).
- `ComponentInspectors`: per-component editing. `RenderComponentInspector` uses a custom inspector registered with `RegisterComponentInspector` when there is one, and otherwise the generic `ImGuiPropertyVisitor` driven by the component's generated `AcceptPropertyVisitor`.
- `ImGuiPropertyVisitor`: the `IPropertyVisitor` implementation that renders ImGui controls for reflected fields.

Rules:
- Keep selection and scene navigation in `SceneEditorWidget`; keep component field editing in `ComponentInspectors`. Do not duplicate edit logic in the scene widget.
- A new editable field only needs `BE_REFLECT_FIELD`; the generic visitor shows it. Write a custom inspector only when the generic one is not enough.
- Scene editor behavior stays aligned with `config/SceneConfig.xml`. New editable scene/component fields are represented there and then surfaced through inspectors.
- Editor panels build their window in `Update()` (see "ImGui in `Update()`" above).
- Save replaces only the `<scene>` of the active scene in `SceneConfig`, matched by its `name` (`WriteSceneToConfig`, `SceneEditor/SceneConfigWriter.h`, no ImGui). The other scenes, their order and the comments in the DOM around them stay (`XmlDocument` loads with the default pugixml flags, which drop comments, so a comment of the file on disk is already gone from the DOM that `SaveConfig` writes); a scene without an element is appended. The scene's attributes come from `Scene::Serialize`, so a new reflected field of `Scene` is saved without a change there; the nodes are still written by hand (`name`, `layer` when not 0, the components through `Serialize`). Tested without ImGui in `tests/BECore/SceneEditorSaveTest.cpp` on a document of its own and a temporary file; never test it through `ConfigManager::SaveConfig` on a shipped config, that overwrites the file in the repository.

## Config Editor

`ConfigEditorWidget` edits any loaded XML config. Field typing comes from `ConfigFieldSchema` (registered fields per config path, for example `LoggerConfig` → `sink/@type`), the controls from `FieldInspectors`, and edits stay in `StagedChanges` until the user saves. New config attributes with a fixed value set should be registered in the schema, in `ConfigEditorWidget::Initialize`:

- a `type` attribute that the loader resolves through `AbstractFactory<Base>` is registered with `RegisterFactoryTypes<Base>(config, "element/@type")`. The options are the class names the factory has registered (sorted), exactly the strings the loader accepts, so the list cannot drift from the code. Do not mirror them in an enum: the `CORE_ENUM` that the generator emits for a `FACTORY_BASE` has other names and another order;
- an attribute read through `EnumUtils<E>` is registered with `RegisterEnum<E>`; `RegisterFlags` takes the flag names of a `|`-joined value.

An `Enum` combo shows a current value that is not among the options (a typo, or a name the code no longer has) instead of an empty box and marks it `(not a known value)`; choosing an option replaces it. `tests/BECore/ConfigFieldSchemaTest.cpp` (only with `BE_WITH_EDITOR`) checks that the options are creatable by the factory and that the shipped `LoggerConfig.xml` uses only offered values. The save path is tested without ImGui in `tests/BECore/ConfigEditorSaveTest.cpp` (only with `BE_WITH_EDITOR`): the edit goes through `StagedChanges::Set` and `ApplyTo` on a copy of the config, `XmlDocument::SaveToFile` writes a temporary file, and the file is loaded back into the real consumer. Never test it through `ConfigManager::SaveConfig` on a shipped config: that overwrites the file in the repository.
