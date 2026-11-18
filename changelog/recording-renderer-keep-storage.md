# RecordingRenderer allocates on every DrawGeometry and drops its storage each frame: a game's alloc test counts the recorder

- Version: 0.4.5
- Entry: recording-renderer-keep-storage
- Date: 2026-11-18
- Bump: patch

## Changed
- `RecordingRenderer::ClearCalls()` keeps the storage of the geometry calls, so from the second equal frame on the recorder allocates nothing. An allocation test of a game calls `ClearCalls()` between frames; `calls.clear()` still gives the storage up.
