# Memory: arenas

Arena allocators for per-frame and per-task scratch data: a bump pointer over a few big blocks, freed all at once. **Not a replacement of the global allocator:** `operator new` stays as it is (the vendored SDL, ImGui and stb keep `malloc`), and an arena only serves the code that asks for it.

## The registry (`ArenaStats.h`)

Every arena publishes its counters (capacity, used, peak, overflows, chunks, resets) into a static table of `ArenaStats::kMaxArenas` slots, `constinit` like `AllocTracker`: an arena on any thread claims a slot without `CoreManager`, a lock or an allocation. A full table is not an error: that arena runs uncounted.

- **Why a table and not a list of arenas:** a reader (`GetArenaStats()`, `GetArenaTotals()`) must never hold a pointer to an arena, because a thread's scratch arena ends with its thread. The reader copies numbers out of slots; a slot that is released or reused while it is copied is dropped from that snapshot (the `epoch` counter), so no entry is torn.
- **Single writer:** the arena that claimed a slot is its only writer, so the publish calls are a relaxed load and store (no read-modify-write on the bump path). Only `ArenaStatsSlot::Claim` and `Release` synchronise.
- `GetArenaStats()` allocates (a `Vector`): never on a frame path. `GetArenaTotals()` does not.
- The arena name is a string literal; the registry keeps the pointer.
