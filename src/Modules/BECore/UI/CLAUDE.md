# UI Module Guidelines

In-game UI drawn through `IRenderer`, in both backends and both run modes. ImGui is not involved (it is the tool UI: `.claude/rules/widgets-module.md`). Authoring decision: `AuthoringDecision.md`.

**Adaptivity is the foundation.** Window size, aspect, orientation, safe area, pixel density and user scale are inputs of every layout; **every screen must pass `ValidateOnMatrix` before it is done**.

```
UICanvasComponent (scene component; reflected fields: space, scaleMode, ...)
  ▼ owns and feeds with the frame's UILayoutContext
UICanvasLayout (pure) ── UICanvasScaler::Resolve → scale, bounds, safeBounds, contentBounds, ViewTransform
                      ── UIVariants::Apply       → `active` style of every element
                      ── UITreeLayout::Run       → `rect` of every element (UILayout: the rules)
                      ── HitTest, Validate (UILayoutValidator)
  ▼
UIElement tree (root = the canvas)
```

## Where to read

Each file below loads by itself when a file of its area is read; read it by path when you write a screen from a game.

| Task | Read |
|------|------|
| Layout rules, variants, the validator, the size matrix, a screen test | `.claude/rules/ui-layout.md` |
| A screen in XML, the controls and their fields, a new control, `UIVars` and `UIStrings` | `.claude/rules/ui-controls.md` |
| `ui_tree` / `ui_click` of the control API | `.claude/rules/ui-control-api.md` |
| A screen in HTML + CSS | `.claude/rules/ui-web.md` |
| Rounded boxes, borders, gradients, shadows | `.claude/rules/ui-shapes.md` |

| Class | Role |
|-------|------|
| `UILayoutContext` | everything layout depends on besides the tree (output size, density, display scale, safe area, user scale, view size, World node box); `operator==` |
| `UIEnvironment` (`CoreManager::GetUIEnvironment()`) | `UISettings` from `config/UIConfig.xml`, `MakeContext`; `SetSafeAreaOverride` / `SetPixelDensityOverride` for the editor viewport |
| `UIElement` | `FACTORY_BASE` node: `name`, `layout`, `style` (authored) / `active` (after variants), `rect`, `children`, `blocksInput`, `interactive`, `ignoreSafeArea`, `color`, virtual `CheckContent()` |
| `UICanvasLayout` | the pure pipeline with a cache, `HitTest`, `Validate` |
| `UIColor`, `UIText` / `UIAttr` | colors and the state shifts; text forms of lengths, edges, colors, numbers |
| `UIDocument`, `UIDocumentLoader` | a `.ui.xml` file, `Instantiate` per canvas |
| `UIRegionSet` | the named rects (canvas units) a game publishes for a canvas; an element with `region` lays out inside one (`ui-layout.md`, "Named regions") |
| `UIRectEdit` | the inverse of `UILayout::ResolveRect` (`SpecForRect`: a wanted rect to a spec, only numbers change; `exact` false when limits or aspect forbid it), the px / % switch that keeps the layout, the region names of a picker |
| `UIAnchorPresets` | the Unity grid of sixteen anchors, `Match` of a spec, `Apply` that changes the anchors and keeps the rect on screen (Shift: pivot, Alt: position) |
| `UIPreview`, `UIPreviewOverlay` | the viewport's shapes, devices, zoom and safe areas; the rects of its overlays (view, canvas bounds, touch marks, regions) |

## Rules

- **UI code reads the context, never the window.** `UICanvasLayout`, `UILayout`, `UICanvasScaler`, `UIVariants` and the validator stay pure (no scene, window or renderer).
- **Edit the tree, then `Invalidate()`.** The cache notices context and canvas-setting changes itself, not element edits. A reflected field changed in the inspector is noticed on the next draw.
- **Draw only in `OnDraw()`**, input only through `UIInput`; `OnDraw()` adds or removes no nodes or components.
- `CoreManager` declares `_uiInput` and `_uiEnvironment` before `_sceneManager`: a canvas unregisters from `UIInput` in its destructor.
- **Widgets own no game state.** A choice group, a dropdown, a toggle and a slider only emit `ActionEvent(action, value)`; the game applies it and sets the bound `UIVars` variable (game → `UIVars` → widget), so the screen cannot disagree with the game; a widget keeps UI state only (hover, press, an open list), never the choice. Publish **ids** in variables, **words** in the string table.
- `UIVars` and `UIStrings` are global: a test clears them in `TearDown`.

## Canvases

| `space` | Fills | Unit | Scale |
|---------|-------|------|-------|
| `Screen` | the render output | by `scaleMode` | mode x user scale, within `minScale` / `maxScale` |
| `SceneView` | the scene's **active** view, fitted (`extendToOutput`: the whole output) | a scene unit | the scene's fit |
| `World` | the node's `TransformComponent` box | a scene unit | the scene's fit; follows the node |

`scaleMode` (Screen only): `ConstantPixelSize`, `ConstantPhysicalSize`, `ScaleWithScreen` (against `referenceWidth` x `referenceHeight`; `match` 0 width, 1 height, between them Unity's geometric blend). `useSafeArea`: `contentBounds` is the safe area, `bounds` stays the canvas (a root child with `ignoreSafeArea` is laid out in `bounds`, and the validator checks it and its descendants against `bounds` only).

- **A scene with view variants** (`Scene/CLAUDE.md`, "View size") picks its view in `Scene::Draw` before any canvas lays out; `MakeContext` reads the active size, so after a switch `bounds` are the new view and the canvas `variants` choose again, with no extra signal.
- **`extendToOutput`** (`SceneView` only; ignored with a warning on the others): `bounds` are the whole output in scene units, with a negative origin where the fitted view leaves bars; `viewBounds` is the view inside them; `transform` stays the scene's fit, so scene points do not move. Everything that lays out (anchors, `x` / `y`, `contentBounds`, `OutsideCanvas`, the variants) uses `bounds`. `RmlUiDocumentComponent` takes the flag too (`RmlUiHost/CLAUDE.md`).
- **Regions** are published by the game through `UICanvasComponent::SetRegion` at every change of its layout; they are not saved and need no `Invalidate()`.
- **Draw order** is the scene's (layer, then scene order); **input order** the reverse (refreshed every draw, also while hidden).
- **Input:** a canvas consumes a mouse event on a hit-testable element (`blocksInput` or `interactive`) by the last draw's layout; hover over one is consumed except a drag that began elsewhere; a press it took is followed to its release; a `modal` canvas consumes everything while shown; hidden, `inputEnabled` off and inactive-scene canvases consume nothing. Hit testing: child before parent, later sibling first; children are not clipped.
- **Popups** (`UIElement::HasPopupChildren`, a dropdown's list): out of the flow, with no rect while closed; open, drawn after the whole tree and hit-tested before it. A left press anywhere else closes the popup and is **consumed** with its release. One popup per canvas (the newest wins).
- **Pointer hooks:** `OnClick` on release over the same element; `OnPress(x, y)` / `OnDrag(x, y)` of an `interactive` element, in canvas units; they run last.
- **Show / hide:** `UICanvasComponent::SetVisibleByName`; one element: `SetElementVisible` / `SetElementVisibleByName` (sets `style.visible`; a variant still wins).
- RmlUi (`RmlUiHost`) is a separate front-end with its own DOM; the editor, the validator and `ui_tree` do not see into it.

## Editor preview

`ViewportWidget` previews other shapes without resizing the window. A size picker offers the panel (Free), the aspect presets, the devices of `UIPreview::GetDevices()` (logical size in portrait, with the cut-out and home bar) and a custom `W x H`; "Rotate" swaps the sides (off for Free). A device or custom size renders the target at exactly that size (clamped 64..4096), so canvases lay out for the real size, and shows it at a zoom of at most 100%; the mouse and the overlays are mapped through the zoom. `SafeAreaSimulation::Device` is the chosen device's cut-out by orientation. During its scene pass the viewport holds `SetSafeAreaOverride` and `SetPixelDensityOverride(1)`, so canvases never see the window's metrics; a device's own pixel density is not simulated (the density stays 1, which is what `minTouchSize` is measured in). The size of the target picks the scene's view variant; a combo forces one through `Scene::SetViewOverride` (`Scene/CLAUDE.md`).

The "Overlays" popup toggles the safe area, the view bounds (the fitted view, bars tinted), the canvas bounds, the touch targets and the regions, all from `UIPreviewOverlay` in output pixels. The regions are what a game published with `SetRegion`: each is outlined under the touch targets with its name, and hovering one (the smallest under the pointer) shows the name and the rect in canvas units; `RegionMarks` keeps a region partly outside the canvas as it is. A touch target is a visible interactive element of a laid-out canvas, red under `minTouchSize` by the rule of `UILayoutValidator`. `TouchTargets` skips an interactive element with an empty rect, while the validator reports it as too small. RmlUi documents are not in the overlays.

The choice (shape, safe area, overlays; not the forced view) is an editor setting: `EditorSettings` stores it in `preferences.json` (`viewportPreview`, the device by name) and it never reaches `Scene::Serialize`. The widget gets it with `EditorPreferencesChangedEvent` and reports a change with `ViewportPreviewChangedEvent`.

## Not done

Regions in the HTML + CSS front-end. Button sounds, focus and keyboard / gamepad navigation, text input, scrolling, clipping of children, a vertical or range slider, a drag over the control API, OS language detection, plural forms, hot reload, the debug overlay.
