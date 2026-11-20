# Memory: arenas

Arena allocators for per-frame and per-task scratch data: a bump pointer over a few big blocks, freed all at once. **Not a replacement of the global allocator:** `operator new` stays as it is (the vendored SDL, ImGui and stb keep `malloc`), and an arena only serves the code that asks for it.

## The registry (`ArenaStats.h`)

Every arena publishes its counters (capacity, used, peak, overflows, chunks, resets) into a static table of `ArenaStats::kMaxArenas` slots, `constinit` like `AllocTracker`: an arena on any thread claims a slot without `CoreManager`, a lock or an allocation. A full table is not an error: that arena runs uncounted.

- **Why a table and not a list of arenas:** a reader (`GetArenaStats()`, `GetArenaTotals()`) must never hold a pointer to an arena, because a thread's scratch arena ends with its thread. The reader copies numbers out of slots; a slot that is released or reused while it is copied is dropped from that snapshot (the `epoch` counter), so no entry is torn.
- **Single writer:** the arena that claimed a slot is its only writer, so the publish calls are a relaxed load and store (no read-modify-write on the bump path). Only `ArenaStatsSlot::Claim` and `Release` synchronise.
- `GetArenaStats()` allocates (a `Vector`): never on a frame path. `GetArenaTotals()` does not.
- The arena name is a string literal; the registry keeps the pointer.

## LinearArena (`LinearArena.h`)

- **Chunks:** the first chunk is a buffer the caller gives or a heap block made by the first `Allocate`; later chunks come from `::operator new` (not `malloc`, so `AllocTracker` shows the few big blocks). A request that does not fit goes to the next chunk kept from an earlier round, else to a new one of `max(chunkSize, size + alignment)` bytes.
- **Overflow:** a new chunk after the first counts one overflow and is never fatal, a full caller's buffer included (heap fallback). The lazily made first chunk is not an overflow. `Reset()` and `Rewind()` keep every chunk (a chunk is reused without a new overflow); `Release()` frees all but the first.
- **No destructors run on `Reset()`:** `Allocate<T>` is `static_assert`ed trivially destructible and returns uninitialised storage. A container on the default allocator that lives in the arena must be destroyed first.
- **Not thread-safe;** only the counters are read across threads. A zero-size request gets one byte, so addresses stay distinct.
- **Marks:** `GetMark()` / `Rewind(mark)` undo in reverse order; a mark from before `Reset()` or `Release()` asserts in Debug (a generation number) and the rewind does nothing. Nothing is allocated and nothing is registered before the first `Allocate`, so an arena can be a `constinit` object.

## Poisoning (`ArenaPoison.h`)

Memory an arena took back (`Reset`, `Rewind`) and fresh chunks are marked in one place: Debug (asserts on) fills `kFreedPattern`, an ASan build poisons the range, Release does nothing. Under ASan every byte past the bump pointer stays poisoned and an allocation unpoisons exactly its bytes (alignment gaps stay poisoned), so a pointer used after `Reset` is reported where it is read.
