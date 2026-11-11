---
paths:
  - "src/Modules/BECore/UI/UILayout*"
  - "src/Modules/BECore/UI/UILength*"
  - "src/Modules/BECore/UI/UIRectSpec*"
  - "src/Modules/BECore/UI/UIRegions*"
  - "src/Modules/BECore/UI/UITreeLayout*"
  - "src/Modules/BECore/UI/UICanvasLayout*"
  - "src/Modules/BECore/UI/UICanvasScaler*"
  - "src/Modules/BECore/UI/UIVariant*"
  - "src/Modules/BECore/UI/UIElementStyle*"
  - "tests/Common/UILayoutMatrix*"
  - "tests/BECore/UILayout*"
  - "tests/BECore/UI*ScreenTest.cpp"
  - "tests/BECore/SceneViewEndToEndTest.cpp"
---

# UI layout, variants and the validator

The module's contract: `src/Modules/BECore/UI/CLAUDE.md`.

## Layout

- **Anchor:** nine points and `Stretch`; the pivot is the same point of the element, so `BottomRight` with `x=-12` moves inwards. `Stretch` fills the parent minus `margin`.
- **Order:** size (units or percent) → `min*` / `max*` (min wins) → aspect (`Fit` / `Fill`) → anchor and offset.
- **Stack:** `direction`, `spacing`, `padding`, `wrap`, `crossAlign`; `flex` shares what is left on a line (clamped, not redistributed). A stack ignores children's anchor, offset, margin and aspect. **`crossAlign="Stretch"` overrides a child's explicit cross size**: a row holding a thin control (slider, progress bar) uses `Center`.
- **Hidden elements** get an empty rect and take no room.
- **Variants:** a condition on canvas `bounds` in units (`orientation` (a square is portrait), aspect, width, height ranges) and overrides of `visible`, `anchor`, `x`, `y`, `width`, `height`, `direction`, `fontSize`, `region` (`UIElementStyle`: what a variant can change). Applied in order (later wins), always from `style`; re-chosen only when the bounds change. Under `extendToOutput` they are conditioned on the output's shape in scene units.

## Validator and the size matrix

Issues: `OutsideCanvas`, `OutsideSafeArea`, `SiblingsOverlap` (in a stack), `TouchTargetTooSmall` (`min(w, h) * scale / pixelDensity` < `minTouchSize`), `TextDoesNotFit` (only content that cannot shrink), `OverlapsSceneView` (below), `MissingRegion`, `OutsideRegion`, `RegionIgnored` ("Named regions" below; `UILayoutIssue::other` holds the region name).

`tests/Common/UILayoutMatrix.h` runs a canvas over the matrix of aspects, insets, scale modes and user scales. A canvas from a scene needs one draw first (its parameters come from the component). One line: `EXPECT_TRUE(ValidateOnMatrix(canvas).empty()) << DescribeMatrixFailures(failures)`.

- **A `SceneView` canvas of a scene with `viewVariants`** passes `UIMatrixViews{viewWidth, viewHeight, variants}` as the fourth argument: each case takes the view `SceneViewSelector::Pick` gives for its output (what `Scene::Draw` picks), and the failure label ends with `view <name>`.
- **`OverlapsSceneView`** (`UILayoutValidatorOptions::keepOffView`, for `extendToOutput` canvases): the caller names the root children (a bar row) whose subtrees must stay off `viewBounds`; an empty list checks nothing. Where the output has no bar, a variant hides the row.
- **A game with two layouts:** `viewVariants` in the scene, one `extendToOutput` canvas with the board corner and a bar row that a variant hides where there is no bar, a screen test with `UIMatrixViews` and `keepOffView` (`SceneViewEndToEndTest.cpp`).

## Named regions

A game publishes **named rectangles in canvas units** for one canvas (`UICanvasComponent::SetRegion` / `RemoveRegion` / `ClearRegions`, a `UIRegionSet`; for a `SceneView` canvas canvas units are scene units): the table, a seat's plate, the board. An element names one with `region="table.plate.1"` (a flat attribute of the style, so a variant can set it too).

- **Anchor layout:** the region replaces the parent box of that element. Anchor, `margin` (`Stretch`), offset and size in units or percent then work inside the region; a region wins over `ignoreSafeArea`; a region of zero width or height is valid (the element gets an empty rect). Descendants follow their own parent, not the region.
- **Stack, popup:** the position is the parent's, so the `region` of a child is ignored (`RegionIgnored`). A stack itself may name a region.
- **Not published:** the element falls back to the parent box, never a crash (`MissingRegion`). The validator also reports `OutsideRegion`: a laid-out element sticks out of its region (the tolerance of the other checks), reported beside `OutsideCanvas`, not instead of it.
- **Published at every change of the game's layout, not saved with the scene:** regions are not reflected fields. A change of the set re-lays the canvas out by itself (no `Invalidate()`); the variants are not chosen again, they depend on `bounds` only.
- **Matrix:** `ValidateOnMatrix(canvas, options, baseContext, views, publishRegions)`; the publisher gets an empty set and the `UICanvasResolved` of each case (the function the game calls itself to compute its regions from the bounds), so "inside its region" is checked on every size. A name the publisher does not set fails every case with `MissingRegion`.
- **Design rule:** an element that must sit on a part of the game's scene names its region, not pixels.

## A screen test

It builds the screen through the reflected path a game uses, sets the variables to their longest values, runs the matrix **once per language**, and also asks `CheckContent(width, height, scale).fits` of every shown label, button and toggle: **an ellipsis label "can shrink", so the validator alone misses a cut word**. It holds a deliberately broken copy for each check. Templates in `tests/BECore/`: `UIScreenTest.cpp` (a HUD), `UISelectorScreenTest.cpp` (a modal panel of choice groups), `UISettingsScreenTest.cpp` (toggle, slider, progress bar, a language group, a long test language), `UIDropdownScreenTest.cpp` (the matrix closed and open).

**Design rules:** anchors and percents, not absolute positions; interactive elements ≥ 48 units at the reference size with a `minScale` that keeps them touch targets; a variant for the shapes that do not fit (usually portrait); text that wraps, cuts with an ellipsis or shrinks.
