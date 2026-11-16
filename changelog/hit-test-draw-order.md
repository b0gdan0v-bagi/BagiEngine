# Input: no single hit test by the real draw order and clipping across scene objects and UI canvases

- Version: 0.1.22
- Entry: hit-test-draw-order
- Date: 2026-11-16
- Bump: patch

## Added
- One hit test by the real draw order: `UIInput` walks scene targets and UI canvases (RmlUi documents too) in the order of `Scene::Draw()` with an O(1) draw rank, holds the pointer capture and sends leave; `PointerTracker` and `HitClip` are pure and tested alone.
- `PointerTargetComponent`, the base for a game component that takes pointer presses, enter, move and leave by its shape; `ClickableComponent` stands on it, with a shape and `blocksInput`.
- `ClipRectComponent`: clipping of a scene subtree, for drawing and for the hit test.
- Multi-touch: finger events are pointers of their own (`PointerId`, `PointerEvent`, `PointerShape`).
- Control API `click_at` and the MCP tool `click_at`; `ui_click` says which target took the click (`takenBy`).

## Changed
- `ClickableComponent` consumes the motion over its rect and its own release, and holds its presses as (pointer, button) pairs; the editor viewport and the SDL provider meet the same order.
