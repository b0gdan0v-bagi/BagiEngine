# Render counters: batch break reasons and upload bytes, skill perf-render

- Version: 0.29.10
- Entry: perf-render-skill
- Date: 2026-11-26
- Bump: patch

## Added
- `BatchBreak` and per-reason batch counts in `RenderCounters` (texture, blend, full, clip, unbatched, boundary), with `textureUploadBytes` and `geometryUploadBytes`; `IRenderer::CountTextureUpload` and `CountGeometryUpload` for backends. The counters reach the profiler trace, the BagiStress line and `headless_frames.py`.
- The skill `perf-render`: CPU- or GPU-bound first, then draw calls, batch breaks, overdraw and uploads.

## Changed
- `IRenderer::Flush()` takes the reason of the break (`Flush(BatchBreak reason = BatchBreak::Boundary)`); a call without it still compiles. Adapt: a game's renderer that flushes for a known reason passes it.
- The benchmarks gate their draw counts exactly (0 % tolerance): one more draw call is a regression.
