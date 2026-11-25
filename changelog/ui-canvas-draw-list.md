# UI: a canvas draws from a flat list rebuilt at a layout run, not by walking its element tree every frame

- Version: 0.22.0
- Entry: ui-canvas-draw-list
- Date: 2026-11-25
- Bump: minor

## Added
- `UIDrawList` (`BECore/UI/UIDrawList.h`): the pre-order draw list of a canvas with its popup sections, rebuilt when the canvas lays out again. Adapt: nothing.

## Breaking
- A UI canvas no longer looks up the transition component and the clicker of each element every frame: they are read when the draw list is rebuilt. Code that adds or removes a `UIInputClicker` or a `UITransitionComponent` in `components` of an element on a shown canvas must call `Invalidate()` of the canvas after the edit, as an edit of the tree already had to; otherwise the new state or transition shows only after the next layout run. Adapt: call `Invalidate()` after such an edit; none of the games does one.
