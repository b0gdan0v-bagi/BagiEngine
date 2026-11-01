# UI Module Guidelines

## Overview

In-game UI that the engine draws through `IRenderer`, in both backends and both run modes. ImGui is not involved (it stays the tool UI: editor panels in `src/Widgets/`, `.claude/rules/widgets-module.md`). Plan and the stages still to come: `.claude/plans/magical-leaping-blossom.md`.

**Adaptivity is the foundation.** Window size, aspect ratio, orientation, safe area, pixel density and a user scale are inputs of every layout, and every screen must pass `ValidateOnMatrix` (below) with no issues before it is considered done.

What exists (plan stage 3): canvases, the scaler, the layout rules, responsive variants, the validator and the host component. What does not exist yet: the controls (panel, label, button), `UIVars`, the XML front-end and the first real screens (stage 4). Elements are plain rects with a color, built in code.

```
UICanvasComponent (scene component, reflected fields: space, scaleMode, ...)
  │  owns, and feeds with the context of each frame
  ▼
UICanvasLayout (pure) ── UICanvasScaler::Resolve(params, UILayoutContext) → scale, bounds, safeBounds, contentBounds, ViewTransform
  │                    ── UIVariants::Apply(root, variants, bounds)      → `active` style of every element
  │                    ── UITreeLayout::Run(root, contentBounds, bounds)  → `rect` of every element  (UILayout: the rules)
  │                    ── HitTest, Validate (UILayoutValidator)
  ▼
UIElement tree (root = the canvas; children are the elements)
```

| Class | Role |
|-------|------|
| `UILength`, `UIRectSpec`, `UIStackSpec` | Data: a size in units or percent; how an element finds its rect (anchor, offset, size, limits, aspect ratio, margins, flex); a stack container |
| `UILayout` | Pure rules: `ResolveRect(parent, spec)` (anchor layout) and `LayoutStack(parent, stack, specs, rects)` |
| `UILayoutContext` | Everything layout depends on besides the tree: output size, pixel density, display scale, safe area, user scale, the scene's view size, the node box of a World canvas. Has `operator==` |
| `UICanvasScaler` | The one pure function `Resolve(params, context)` → `UICanvasResolved` |
| `UIEnvironment` (`CoreManager::GetUIEnvironment()`) | `UISettings` from `config/UIConfig.xml` (`userScale`, `minTouchSize`) and `MakeContext(window, outputW, outputH)`: puts the window's metrics into a context. `SetSafeAreaOverride()` is for the editor viewport |
| `UIElement` | A node: `name`, `layout` (Anchor or Stack), `style` (authored) and `active` (after variants), `rect` (laid out), `children`, `blocksInput`, `interactive`, `ignoreSafeArea`, `color`; virtual `CheckContent()` |
| `UIVariant` | `when` (`UIVariantCondition`) + `overrides` per named element (`UIElementOverride`); `UIVariants::Apply` |
| `UITreeLayout::Run` | Lays out a whole tree |
| `UICanvasLayout` | The pure pipeline above with a cache, `HitTest`, `Validate`, the counters tests read. Public fields: `params`, `root`, `variants` |
| `UILayoutValidator` | `Validate(root, resolved, options)` → `UILayoutIssue`s |
| `UICanvasComponent` | The scene host: draw, `UIInput` handler, show / hide by name |
| `UIPreview` | The numbers of the editor viewport preview: preset shapes and simulated safe areas |

## Canvases

A scene may hold any number of canvases (a `UICanvasComponent` on a node each). `space` picks what a canvas fills:

| Space | Fills | UI unit | Scale |
|-------|-------|---------|-------|
| `Screen` | the render output, so its edges are the window's edges | by `scaleMode` | mode scale x user scale, kept between `minScale` and `maxScale` |
| `SceneView` | the scene's fitted view (`viewWidth` x `viewHeight`) | a scene unit | the fit of `Scene::Draw()` (`ViewTransform::Fit`) |
| `World` | the box of the node's `TransformComponent` | a scene unit | the same fit; the canvas origin is the node's position, so it follows the node |

`scaleMode` (Screen only): `ConstantPixelSize` (a unit is a window unit, so the scale is the pixel density), `ConstantPhysicalSize` (that times the display scale), `ScaleWithScreen` (follows the output size against `referenceWidth` x `referenceHeight`; `match` 0 follows the width, 1 the height, between them the geometric blend like Unity's). `SceneView` and `World` ignore the mode, the user scale and the limits: the board they belong to is scaled by the scene's fit. A scene without a view size has the identity fit, so a SceneView canvas is then the output at scale 1. `useSafeArea` applies to Screen and SceneView: `contentBounds` is the safe area, and `bounds` stays the whole canvas for backgrounds (`UIElement::ignoreSafeArea` on a child of the root lays it out against `bounds`).

- **Draw order** is the scene's: node `layer`, then scene order. `OnDraw()` sets the canvas's own `ViewTransform` for its elements and gives the scene's back.
- **Input order** is the reverse. The component registers with `UIInput` with `Scene::GetDrawRank(node)` as the order and refreshes it on every draw (also while hidden), because a scene attaches its nodes one by one while it fills its tree.
- **Input rules:** a canvas consumes a mouse event that lands on a hit-testable element (`blocksInput` or `interactive`), by the layout of its last draw; motion over one is a hover and is consumed, except a drag that began elsewhere (a button held whose press this canvas did not take) which is the game's; a press the canvas took is followed to its release wherever the pointer goes; a `modal` canvas consumes everything while shown. A hidden canvas, one with `inputEnabled` off and one of an inactive scene consume nothing. Hit testing finds the topmost element: a child before its parent, a later sibling before an earlier one; the rect of a child is not clipped by its parent.
- **Visibility by name:** `UICanvasComponent::SetVisibleByName(scene, nodeName, visible)`.
- The reflected fields are named `visible` and `inputEnabled`, not `enabled`: a vector of factory-made components writes an `enabled` attribute of its own on every element (`Reflection/DataAccessor.h`), which a field of that name would collide with.

## Layout rules (`UILayout`)

All rects are in the UI units of one canvas.

- **Anchor layout.** `UIAnchor`: the nine points and `Stretch`. The element's pivot is the same point of itself, so `BottomRight` with `x = -12, y = -10` moves up and left into the parent. `Stretch` fills the parent minus `margin` and ignores offset and size.
- **Order of the steps:** size (`UILength`: units, or a percent of the parent on the same axis) → `min*`/`max*` limits (the minimum wins when they disagree) → aspect ratio (`Fit`: the largest rect of the ratio inside the box, `Fill`: the smallest that covers it) → pin to the anchor and offset. A Stretch element that a limit made smaller is centered in its area.
- **Stack.** `UIStackSpec`: `direction`, `spacing`, `padding`, `wrap`, `crossAlign` (`Start`, `Center`, `End`, `Stretch`). A child's main size is its spec size clamped by its limits; `flex` > 0 takes a share of what the fixed children and the spacing leave on its line (shares follow the weights; each is clamped by the child's limits and the clamp does not redistribute). With `wrap` a child starts a new line when its spec size does not fit the rest of the line (a flex child counts with its spec size there); without it the line is as wide as the content on the cross axis. A stack ignores its children's anchor, offset, margin and aspect ratio.
- **Hidden elements** (`style.visible` false, or an ancestor's) get an empty rect and take no room in a stack.

## Responsive variants

A canvas holds `variants` (`UICanvasLayout::variants`): `UIVariant { when, overrides }`. `when` is a condition on the canvas `bounds` in UI units: `orientation` (portrait is height >= width, a square is portrait, like CSS), an aspect range, width and height ranges (a limit of 0 is off, ends are included; every set part must hold). An override names an element and replaces some of: `visible`, `anchor`, `x`, `y`, `width`, `height`, stack `direction`, `fontSize`. Variants apply in list order, so a later one wins like CSS, and `Apply` always starts from `style`, so leaving a shape restores the authored values. The variants are chosen again only when the bounds change (or after `Invalidate()`): a new safe area or a new density that leaves the bounds alone only lays the tree out again (`UICanvasLayout::GetVariantRuns()` / `GetLayoutRuns()`).

## Validator and the size matrix

`UILayoutValidator::Validate` checks a laid-out canvas, visible elements only:

| Issue | Meaning |
|-------|---------|
| `OutsideCanvas` | An element sticks out of the canvas (by more than 0.01 unit) |
| `OutsideSafeArea` | Inside the canvas but outside `contentBounds` (not for `ignoreSafeArea` children of the root) |
| `SiblingsOverlap` | Two children of a stack overlap |
| `TouchTargetTooSmall` | An `interactive` element is smaller than `minTouchSize` **as the user sees it**: `min(w, h) * scale / pixelDensity` in window units |
| `TextDoesNotFit` | `CheckContent()` says there is content that does not fit and cannot shrink (ellipsis or auto-fit) |

`tests/Common/UILayoutMatrix.h` runs a canvas over the matrix: ten aspect ratios (16:9, 16:10, 3:2, 4:3, 1:1, 21:9, 32:9, 9:16, 9:19.5, 3:4) at short sides of 480 to 2160 px (long side up to 3840), with no insets, a top notch and side insets, with each scale mode and user scales 1.0 and 1.5 for a Screen canvas. A screen test is one line: `EXPECT_TRUE(ValidateOnMatrix(canvas).empty()) << DescribeMatrixFailures(failures)`. The matrix finds real defects. For example a 48-unit button on a `ScaleWithScreen` canvas (reference 1280 x 720) is only 24 px on a 480 x 480 window, below the 32 window units of `minTouchSize`; the fixes are a bigger button, or `minScale` (0.7 keeps 48 units above 32 px).

**Design rules the matrix enforces:** use anchors and percents, not absolute positions; give interactive elements at least 48 units at the reference size and set `minScale` so they stay touch targets; give a screen a variant for the shapes where it does not fit (a portrait canvas is the usual one); let text wrap, cut with an ellipsis or shrink.

## Building a screen in code (until the XML front-end)

```cpp
void MyHud::OnAttached() {  // the node has a UICanvasComponent before this component in its list
    auto canvas = GetNode().GetComponent<UICanvasComponent>();
    auto hud = canvas->GetRoot().AddChild("Hud"_intern);
    hud->style.rect.x = 16.0f;
    hud->style.rect.y = 16.0f;
    hud->style.rect.width = UILength::Px(220.0f);
    hud->style.rect.height = UILength::Px(96.0f);
    hud->color = Color{0, 0, 0, 160};
    hud->blocksInput = true;
    canvas->Invalidate();  // after editing the tree or the variants
}
```

```xml
<node name="Hud" layer="10"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="0.7" useSafeArea="true"/>
</components></node>
```

The elements and variants are not reflected yet: they cannot be written in `SceneConfig.xml` until stage 4 (task 4.1 checks that nested children round-trip).

## Editor preview

`ViewportWidget` has a toolbar: an aspect preset (Free, 16:9, 4:3, 21:9, 9:16) that fits the image into the panel with that shape, and a safe-area simulation (None, Notch, Side insets) that is drawn as an outline. The viewport is not the window, so during the scene pass `UIEnvironment::SetSafeAreaOverride()` holds the simulated safe area (the whole target for None), and canvases never see the window's. The numbers come from `UIPreview` (`UIPreviewTest`), and the matrix of the tests uses the same safe-area shapes. Editor input goes through `ViewportWidget::ForwardMouse` into `UIInput`.

## Rules

- **UI code reads the context, never the window.** Everything layout needs is in `UILayoutContext`; a pure test sets it by hand.
- **`UICanvasLayout`, `UILayout`, `UICanvasScaler`, `UIVariants` and the validator stay pure:** no scene, window or renderer, so they are tested without one.
- **Edit the tree, then `Invalidate()`.** The cache notices a changed context and changed canvas settings by itself, not edits of elements.
- **A change of a reflected field of the component** (inspector) is noticed on the next draw (the fields are copied into `params` and compared).
- **Draw only in `OnDraw()`**, input only through `UIInput` (`BECore/Input/UIInput.h`); `OnDraw()` must not add or remove nodes or components.
- `CoreManager` declares `_uiInput` and `_uiEnvironment` before `_sceneManager`, so they outlive the scenes: a canvas unregisters from `UIInput` in its destructor.

## Not done

Controls (`UIPanel`, `UIImage`, `UILabel`, `UIButton`), `UIVars`, `UIDocument` and the XML front-end, reflection of elements and variants, clipping of children to their parent, scrolling, focus and gamepad navigation, a `ClickableComponent` replacement for buttons, the debug overlay and the control API methods (`ui_tree`, `ui_click`). The first two plus the XML front-end are stage 4 of the plan.

## Tests

`tests/BECore/`: `UILayoutTest` (anchors, stretch, limits, aspect, stack), `UICanvasScalerTest` (every space and mode, safe area, the context comparison), `UISettingsTest` (settings, `UIEnvironment`), `UIVariantTest` (conditions, order, restoring, tree layout), `UICanvasLayoutTest` (cache, variant runs, hit testing, the matrix itself and shipped-style screens), `UILayoutValidatorTest`, `UICanvasComponentTest` (a scene from XML into a `RecordingRenderer`: drawing in each space, the view transform handed back, input order, press following, modal, inactive scenes), `UIPreviewTest`.
