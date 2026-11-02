---
paths:
  - "src/Widgets/**/*"
  - "src/Modules/BECore/Widgets/**/*"
---

# Widget System Guidelines

## Overview

Widgets are the engine's per-frame building blocks implementing `IWidget` (a `FACTORY_BASE` that also derives `SubscriptionHolder`). `WidgetManager` creates them from XML and drives them every frame. There are two kinds:

- **Editor panels**: `SceneEditorWidget`, `ConfigEditorWidget`, `TextureEditorWidget`, `EditorLayoutWidget`, `ViewportWidget`. They build ImGui windows. `ViewportWidget` also has aspect presets and a safe-area simulation for looking at a UI layout in several shapes (`src/Modules/BECore/UI/CLAUDE.md`, "Editor preview").
- **Frame widgets**: steps of the render frame that are not UI. `ClearScreenWidget` clears the back buffer and opens the swapchain pass, `SceneRenderWidget` draws the scene (Game mode), `ImGuiWidget` begins and renders the ImGui frame (Editor mode).

A widget is not game UI. In-game UI is a scene feature, canvases of `UICanvasComponent` (`src/Modules/BECore/UI/CLAUDE.md`); do not build it on `IWidget`.

## IWidget Interface

All methods are `BE_FUNCTION`.

| Method | Purpose |
|--------|---------|
| `Initialize(IDeserializer&)` | Read the widget's own `<widget>` node, subscribe to events; return `false` on failure (the widget is then logged and not registered) |
| `Update()` | Every frame. An ImGui panel builds its window here (`ImGui::Begin` ... `End`), between `ImGui::NewFrame` and `ImGuiWidget::Draw`. A frame widget issues its render commands here when they must come first (`ClearScreenWidget`) |
| `DrawScreen()` | Optional, default is empty. Draws into the back buffer after every `Update()` (`SceneRenderWidget` draws the scene here) |
| `Draw()` | Every frame, after `DrawScreen()`. Normally empty; `ImGuiWidget::Draw()` runs `ImGui::Render()` and the ImGui draw event |

## Widget Lifecycle

1. `WidgetManager::CreateWidgets()` (from `CoreManager::OnApplicationInit`) reads the run mode (`mode` in `ApplicationConfig.xml`, see `BECore/CLAUDE.md`) and creates each widget of that mode's config through `AbstractFactory<IWidget>` using the default constructor.
2. `Initialize(deserializer)`: load config, subscribe to events. An unknown `type` or an `Initialize` that returns `false` is logged (`WidgetManager:` error) and the widget is skipped.
3. Every frame, from `CoreManager::OnGameCycle`: `UpdateAll()` → (queued events) → `DrawScreenAll()` → `DrawAll()`, in registration order.
4. Destruction: `SubscriptionHolder` drops the event subscriptions.

## XML Configuration

File: `config/WidgetsConfig.xml` (Editor mode) or `config/GameWidgetsConfig.xml` (Game mode: `ClearScreenWidget` + `SceneRenderWidget`, no ImGui, so a Game-mode widget must not touch ImGui). Entries `<widget type="..."/>` under `<widgets>`:
- `type`: widget class name (`ImGuiWidget`, `EditorLayoutWidget`, `ViewportWidget`, `SceneRenderWidget`, ...)
- widget-specific settings as child elements or attributes (for example `<clearColor r g b a/>`), read in `Initialize`
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

## Config Editor

`ConfigEditorWidget` edits any loaded XML config. Field typing comes from `ConfigFieldSchema` (registered fields per config path, for example `LoggerConfig` → `sink/@type`), the controls from `FieldInspectors`, and edits stay in `StagedChanges` until the user saves. New config attributes with a fixed value set should be registered in the schema, in `ConfigEditorWidget::Initialize`:

- a `type` attribute that the loader resolves through `AbstractFactory<Base>` is registered with `RegisterFactoryTypes<Base>(config, "element/@type")`. The options are the class names the factory has registered (sorted), exactly the strings the loader accepts, so the list cannot drift from the code. Do not mirror them in an enum: the `CORE_ENUM` that the generator emits for a `FACTORY_BASE` has other names and another order;
- an attribute read through `EnumUtils<E>` is registered with `RegisterEnum<E>`; `RegisterFlags` takes the flag names of a `|`-joined value.

An `Enum` combo shows a current value that is not among the options (a typo, or a name the code no longer has) instead of an empty box and marks it `(not a known value)`; choosing an option replaces it. `tests/BECore/ConfigFieldSchemaTest.cpp` checks that the options are creatable by the factory and that the shipped `LoggerConfig.xml` uses only offered values.
