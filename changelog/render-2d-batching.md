# Renderer: consecutive 2D quads of the same texture and state go to the backend as one batch

- Version: 0.23.0
- Entry: render-2d-batching
- Date: 2026-11-25
- Bump: minor

## Added
- A 2D batcher in `IRenderer`: consecutive rects and textured quads with the same texture, blend mode and view are appended to one batch and submitted once; any state change (texture, blend, clip, view, render target, clear) flushes first, so the draw order is exact. `SetBatchingEnabled(bool)` turns it off for a renderer. The SDL backend submits a batch with `SDL_RenderGeometryRaw`; on the software renderer it batches only the draws proven byte-equal and draws the rest one by one. The Vulkan backend submits a batch as one geometry draw. `GeometryVertex` has its own header `BECore/Renderer/GeometryVertex.h`, included by `IRenderer.h`. Adapt: nothing.
- `RenderCounters::primitives` counts what was asked of the renderer; `frame_stats` of the control API and the profiler overlay show it. Adapt: nothing.

## Changed
- `RenderCounters::drawCalls` (and `drawCalls` of `frame_stats`, the `draw_calls` metric of a perf set) now counts backend submissions, so it drops where draws are batched; the old meaning is `primitives`. Adapt: a test or a perf note that reads `drawCalls` as "one per sprite" reads `primitives` instead; the info-only `draw_calls` metric of a game's perf set may show a change at the next run.
