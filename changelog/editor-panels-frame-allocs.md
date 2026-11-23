# Command palette, Config Editor, profiler panels and game overlay allocate nothing in a steady frame

- Version: 0.11.4
- Entry: editor-panels-frame-allocs
- Date: 2026-11-23
- Bump: patch

## Added
- Forms of the profiler helpers that keep a caller's buffer: `FrameStatsCollector::GetHistoryInto`, `RecentCount`, `ForEachRecent`; `LayoutLanesInto`; `FormatOverlayLinesTo` (each line through a stack buffer, cut at `kOverlayLineCapacity`). The by-value forms stay.
- `FilterCommandsInto` and `CommandMatchList`, the palette's filter recomputed only when the query or the command table changes.

## Changed
- The command palette, the Config Editor, the Profiler window with its timeline and the in-game profiler overlay no longer allocate every frame while open. The ImGui ids of the Config Editor's controls changed (`PushID` instead of formatted labels); nothing is saved under them.
