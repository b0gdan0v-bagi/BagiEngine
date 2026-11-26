# SceneDrawListBuildTest measures the rebuild alone

- Version: 0.29.4
- Entry: scene-draw-list-rebuild-alloc-test
- Date: 2026-11-26
- Bump: patch

## Changed
- `SceneDrawListBuildTest.ARebuildAllocatesNothingOnceTheCapacityIsThere` clears its `RecordingRenderer` before it counts, so it is green in `macos-profile`; the rebuild of the draw list allocates nothing. Adapt: nothing.
