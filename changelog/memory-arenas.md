# Memory: no arena allocators (linear, scratch with markers, EASTL adapter)

- Version: 0.6.10
- Entry: memory-arenas
- Date: 2026-11-20
- Bump: patch

## Added
- `BECore/Memory/`: `LinearArena` (bump allocation with marks, chunk growth and poisoning in debug), the per-thread `ScratchArena` with `ScratchScope`, and the EASTL `ArenaAllocator` with the aliases `ArenaVector<T>` and `ArenaString` (`Types/Containers.h`, `Types/Strings.h`). `Memory/CLAUDE.md` gives the contracts.
  Adapt: nothing; temporary per-frame or per-load data of a game can live in a `ScratchScope` instead of the heap.
- `ArenaStats`: lock-free counters of every arena (capacity, used, peak, overflows, chunks), read through `AllocTracker::GetArenaStats()`; `FrameStats` has `arenaUsedBytes`, `arenaPeakBytes`, `arenaOverflows`, and the control method `frame_stats` answers them and an `arenas` list.
  Adapt: nothing; a tool that reads `frame_stats` may show the new fields.
