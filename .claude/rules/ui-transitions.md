---
paths:
  - "src/Modules/BECore/UI/UITransition*"
  - "src/Modules/BECore/UI/UIStateBlend*"
  - "tests/BECore/UITransition*"
  - "tests/BECore/UIStateBlend*"
  - "**/*.ui.xml"
---

# UI: opacity, show / hide transitions and the smooth state

The module's contract: `src/Modules/BECore/UI/CLAUDE.md`. Everything here is **off by default** (`opacity` 1, duration 0, `stateDuration` unset): a screen that says nothing draws and reacts as it always did. The HTML spelling is in `ui-web.md`.

## Opacity is paint

`IRenderer::SetOpacity` is multiplied into the alpha of every primitive (rects, textures, transformed textures, geometry, so text, boxes, RmlUi and particles too), once per primitive, in SDL, Vulkan and `RecordingRenderer`; `ScopedOpacity` nests by multiplying. `UIElement::opacity` and `UICanvasComponent::_opacity` use it for a whole subtree. **The limit, chosen on purpose:** this is the per-draw alpha multiply of Unity's `CanvasGroup`, not an offscreen composite, so children that overlap inside a faded group blend through each other mid-fade (a panel's text shows its plate a little; `UITransitionCaptureTest` pins the pixel). An exact composite needs a render target in the middle of a frame (`SDLRendererBackend::SetRenderTarget` is a stub, a Vulkan target cannot be made inside a pass): a backend entry of its own, set aside until a screen shows the seam. An element at 0 is still laid out, hit and listed in `ui_tree`; hide with `visible`.

## The spec and the run

`UITransitionSpec` is the data (fade, `scaleFrom` about the centre, slide, durations, `EaseType`, input flag) and `Bounded()` is the one gate for numbers from a file (NaN, negative, huge). `UITransition::Evaluate` / `Compose` are pure: a progress gives opacity, scale and offset, which compose with the view transform. `UITransitionRun` keeps **timestamps of game time** (`CoreManager::GetTime().GetTotalTime()`), not ticks: the answer for a time is the same however often a frame is drawn, and a held frame of a paused game stands still. A reversal starts from the progress shown and takes the new direction's duration in proportion to the distance left. The first `Sync` snaps (nothing animates on load); a duration of 0 snaps.

## Show / hide

- **The authored state is the target.** `visible` (after variants) says where the element goes; `UITransitionComponent` and the canvas `_transition*` fields only delay the effect, so `SetElementVisible`, `SetVisibleByName`, a field edit with `Invalidate()` and variants need no new call.
- **Leaving keeps the rect.** While it leaves, the layout keeps the element visible, so it keeps its rect and is drawn; the canvas lays out again when the run ends (`AdvanceTransitions`) and the element goes for good. A hidden element is laid out empty as ever.
- **The rect is not scaled or slid for input.** Hit testing uses the laid-out rect.
- **Input.** An element in a transition takes no hover, press, click or tooltip unless `inputDuringTransition`, but still consumes events that land on a hit-testable element (`inputLocked`), so what lies below stays shielded. **A canvas that starts to leave releases input at once** (a modal frees the screen), unless `_transitionInput`; a canvas that is showing without it consumes and delivers nothing.
- A transition never makes the pop of before worse: duration 0, a spec that is not active, a component with no duration all mean the change shows in the same frame, as before.

## The smooth state

`UISettings::stateTransition` (`config/UIConfig.xml`) and the per-element `stateDuration` (-1, the default, follows the setting; 0 is off) ease the **state look** from the look shown to the new one: the box fill, border and shadow, the tint of an image and the colour of a text, through `UIStateBlend` (`UIBlend<T>`, a fixed `QuadOut`). A new target starts from what is shown, so a hover cut by a press is continuous; the first draw snaps. Not animated: a toggle's switch, a slider's thumb, a dropdown's open / close, the rect (no layout animation) and RmlUi documents (they have their own `opacity` and `transition`).

## Allocations

A steady frame allocates nothing: a run with no change costs a compare, a blend with duration 0 returns the target untouched. During a transition a text is laid out again at the changing scale (it allocates then, as any text whose key moves does); the game's allocation tests cover steady frames.

## Testing

Drive the **manual clock**: `CoreManager::GetTime().Advance(step)` (at most `Time::kMaxFrameTime`, a quarter of a second, per call: a longer wait is several steps; use steps exact in binary such as 0.125), then draw. `UITransitionComponentTest` checks what is drawn, the layout at the start and the end and who gets input; `UITransitionCaptureTest` reads pixels of an SDL software capture (`BE_UI_TRANSITION_PNG` names a folder to look at the PNGs); the pure rules are `UITransitionTest` and `UIStateBlendTest`.
