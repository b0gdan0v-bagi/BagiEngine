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

## Scratch arena and scopes (`ScratchArena.h`)

`ScratchArena::Get()` is the calling thread's arena, a `constinit thread_local LinearArena` and not a `CoreManager` service: it works on any thread, in a static initialiser or before the engine starts, and allocates nothing until its first `Allocate`. The thread's end frees its chunks and releases its stats slot (all scratch arenas are named `Scratch`, one entry per live thread that used it).

`ScratchScope` takes a mark and rewinds in its destructor; scopes nest like locals. **Rules:** memory of a scope must not outlive it (not returned, not stored in a longer-lived container) and must not cross to another thread; a scope ends on the thread that made it (Debug asserts). Nothing from the arena is destroyed on rewind (`LinearArena`, "No destructors").

## ArenaAllocator (`ArenaAllocator.h`)

The EASTL allocator over a `LinearArena&`, behind `ArenaVector<T>` and `ArenaString` (declared in `Types/`, which keeps `eastl::` and the arena headers out of the PCH). Another container takes it as its allocator parameter (`eastl::hash_map<K, V, Hash, Equal, ArenaAllocator>`).

- **`deallocate` does nothing,** so a vector that grows by doubling leaves every old block in the arena until `Reset()` or the scope's rewind: `reserve` first when the size is known. The container's destructor frees nothing, but a container of non-trivial elements still runs their destructors.
- **A default-constructed allocator binds to the thread's `ScratchArena`** (EASTL needs a default constructor, and builds one from a name string, which is not kept). Such a container belongs to the scope that is alive when it is made and must not cross threads; a container of a longer life takes its arena explicitly.
- **Equality is by arena.** `offset` of the aligned `allocate` is ignored (EASTL passes 0).

## Counters

`ArenaStats` is the only source. `AllocTracker::GetArenaStats()` forwards its snapshot (a thin forwarder, so tools that already read the allocation counters find the arenas there). `FrameStats` takes `arenaUsedBytes`, `arenaPeakBytes` and `arenaOverflows` at `EndFrame`, summed over the arenas alive at that moment (`GetArenaTotals`, no allocation); the overflows are the running total, not per frame. The control method `frame_stats` answers them per frame and adds a top-level `arenas` array (`name, capacity, used, peak, overflows, chunks`). The method `memory_stats` and the editor panel are the entry `editor-memory-panel`.
