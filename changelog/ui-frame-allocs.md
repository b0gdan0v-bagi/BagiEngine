# UI and editor widgets: a steady frame allocates and interns (popups, tooltip, slider, UIVars, inspector, viewport, profiler overlay)

- Version: 0.7.5
- Entry: ui-frame-allocs
- Date: 2026-11-20
- Bump: patch

## Added
- `ScopedAllocCountersPause` (`Profiler/Alloc/AllocTracker.h`): keeps the calling thread's allocations and frees out of every count while alive (scopes nest); the profiler overlay and panel use it, so they no longer show up in the numbers they print. A no-op without `ENGINE_PROFILER_ALLOC`.
  Adapt: nothing; a game's own debug view may wrap itself in it.
- `UIPreviewOverlay::TouchTargets` and `RegionMarks` have forms that fill a caller's vector.
  Adapt: nothing.

## Changed
- A continuous slider (`step` 0) snaps to 1000 steps of its range (`UISliderComponent::kContinuousSteps`) and emits only when the value changed, so a drag no longer interns a new text per pixel.
  Adapt: nothing, unless a game needs finer than 1/1000 of the range from a continuous slider: give it an explicit `step`.
- `UIVars::Expand` no longer interns the names it meets: an unknown `{name}` (never interned) expands to nothing, as before.
  Adapt: nothing.
- A steady frame with an open popup, a tooltip, a selection in the editor or a viewport overlay allocates nothing in those paths; the style cascade budget of the web UI compiler counts the work per selector and compound, so a hostile sheet is refused sooner.
  Adapt: nothing; a web UI sheet near the cascade limit may now be refused with the cascade-work error.
