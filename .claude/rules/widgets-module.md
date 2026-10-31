---
paths:
  - "src/Widgets/**/*"
  - "src/Modules/BECore/Widgets/**/*"
---

# Widget System Guidelines

## Overview

Widgets are UI/rendering components implementing `IWidget` (a `FACTORY_BASE` that also derives `SubscriptionHolder`). `WidgetManager` creates them from XML and drives them every frame.

## IWidget Interface

All methods are `BE_FUNCTION`.

| Method | Purpose |
|--------|---------|
| `Initialize(IDeserializer&)` | Read the widget's own `<widget>` node, subscribe to events; return `false` on failure |
| `Update()` | Every frame, logic |
| `DrawScreen()` | Optional, screen-level drawing before ImGui; default is empty |
| `Draw()` | Every frame, ImGui/rendering |

## Widget Lifecycle

1. `WidgetManager::CreateWidgets()` (from `CoreManager::OnApplicationInit`) reads the run mode (`mode` in `ApplicationConfig.xml`, see `BECore/CLAUDE.md`) and creates each widget of that mode's config through `AbstractFactory<IWidget>` using the default constructor.
2. `Initialize(deserializer)`: load config, subscribe to events.
3. Every frame, from `CoreManager::OnGameCycle`: `UpdateAll()` → (queued events) → `DrawScreenAll()` → `DrawAll()`, in registration order.
4. Destruction: `SubscriptionHolder` drops the event subscriptions.

## XML Configuration

File: `config/WidgetsConfig.xml` (Editor mode) or `config/GameWidgetsConfig.xml` (Game mode: `ClearScreenWidget` + `SceneRenderWidget`, no ImGui, so a Game-mode widget must not touch ImGui). Entries `<widget type="..."/>` under `<widgets>`:
- `type`: widget class name (`ImGuiWidget`, `EditorLayoutWidget`, `ViewportWidget`, `SceneRenderWidget`, ...)
- widget-specific settings as child elements or attributes (for example `<clearColor r g b a/>`), read in `Initialize`
- the file order is the creation and update order; there is no `name` or `priority` attribute

## Best Practices

- **Single Responsibility**: one widget, one purpose.
- **XML Configuration**: tunable parameters come from the widget's XML node.
- **Event-Driven**: communicate through events, not direct widget-to-widget calls.
- **Lazy Initialization**: set up resources in `Initialize()`, not in the constructor.
- **Separation**: `Update()` for logic, `Draw()` for rendering.
- ImGui widgets call `ImGui::Begin()` / `ImGui::End()` inside `Draw()`.

## Scene Editor Conventions

Scene editor code is split across focused layers:

- `SceneEditorWidget`: orchestration (selection flow, scene-level actions, window structure).
- `ComponentInspectors`: per-component editing. `RenderComponentInspector` uses a custom inspector registered with `RegisterComponentInspector` when there is one, and otherwise the generic `ImGuiPropertyVisitor` driven by the component's generated `AcceptPropertyVisitor`.
- `ImGuiPropertyVisitor`: the `IPropertyVisitor` implementation that renders ImGui controls for reflected fields.

Rules:
- Keep selection and scene navigation in `SceneEditorWidget`; keep component field editing in `ComponentInspectors`. Do not duplicate edit logic in the scene widget.
- A new editable field only needs `BE_REFLECT_FIELD`; the generic visitor shows it. Write a custom inspector only when the generic one is not enough.
- Scene editor behavior stays aligned with `config/SceneConfig.xml`. New editable scene/component fields are represented there and then surfaced through inspectors.
- Keep Update/Draw separation in editor widgets too (state changes in update, rendering in draw).

## Config Editor

`ConfigEditorWidget` edits any loaded XML config. Field typing comes from `ConfigFieldSchema` (registered enums per config path, for example `LoggerConfig` → `sink/@type`), the controls from `FieldInspectors`, and edits stay in `StagedChanges` until the user saves. New config attributes with a fixed value set should be registered in the schema.
