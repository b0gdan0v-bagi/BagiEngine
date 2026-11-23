# Allocation tracking (`ENGINE_PROFILER_ALLOC`)

The tracker counts what goes through the global `operator new` and what the vendored C libraries allocate with their own malloc. Without the option every count is zero and nothing here costs anything. Profiler overview: `../CLAUDE.md`; the cost of a count: `../HowToProfile.md`.

## What is tracked and how

- `GlobalOperatorNew.cpp` replaces every form of the global `operator new` / `delete` (plain, array, nothrow, sized, aligned) and is compiled with the option **or on Windows in every build**: without the option macOS, Linux and the web keep the C++ runtime's operators and every count is zero, while on Windows it makes no tracker call and gives every block one free path (below). The AddressSanitizer build (`windows-clang-asan`) is left out: the sanitizer defines these operators itself (a duplicate symbol), and it reports EASTL's aligned `new` / plain `delete` pair as `new-delete-type-mismatch`. `BECoreModule` is linked `WHOLE_ARCHIVE`, so the replacement reaches every executable of the engine and of a game. Covered through it: our code, EASTL (`Utils/EASTLOperators.cpp` forwards to it), `std`, RmlUi.
- **Libraries through `AllocRouting`** (`AllocRouting.h`): `Malloc` / `Calloc` / `Realloc` / `Free (AllocSource, ...)`. With the option they call libc and report; without it they are the inline plain C functions (no tracker call). Each library has an `AllocSource`:

  | Source | Library | Hooked by |
  |--------|---------|-----------|
  | `GlobalNew` | `operator new` | `GlobalOperatorNew.cpp` |
  | `Pugixml` | pugixml | `pugi::set_memory_management_functions` |
  | `ImGui` | Dear ImGui | `ImGui::SetAllocatorFunctions` (its user-data pointer is passed through unused) |
  | `SDL` | SDL3 | `SDL_SetMemoryFunctions` (malloc, calloc, realloc, free) |
  | `Stb` | stb_truetype (`Text/Font.cpp`), stb_image (`CoreSDL/ImageFile.cpp`), stb_vorbis (`Audio/AudioDecode.cpp`) | `STBTT_malloc` / `STBTT_free`, `STBI_MALLOC` / `STBI_REALLOC` / `STBI_FREE`, and function-like `malloc` / `realloc` / `free` macros around `#include <stb_vorbis.c>` |
  | `DrLibs` | dr_flac (`Audio/AudioDecode.cpp`) | `DRFLAC_MALLOC` / `DRFLAC_REALLOC` / `DRFLAC_FREE` |

  The include-site macros are described in `stb/README.md` and `dr_libs/README.md`. The decoded buffer of `stb_vorbis_decode_memory` is freed with `AllocRouting::Free(AllocSource::Stb, ...)`.
- **`Install()`** (idempotent, thread-safe) sets the three library setters. One static initializer in `AllocRouting.cpp` runs it before `main`, because SDL's setter is unsafe once SDL has allocated and tests and tools share no start-up function. **It does nothing without the option**: the libraries keep malloc and the shipped build is unchanged. `IsInstalled()` is true only in a build with the option.
- **A routed block is a plain libc block**, never an `AllocPlatform` one (on Windows those are `_aligned_malloc` memory), and its usable size comes from `AllocPlatform::LibcUsableSize` (`malloc_size` / `malloc_usable_size` / `_msize`). A `Realloc` of a moved or resized block is one free (old usable size) and one alloc; a failed one reports nothing; `Realloc(nullptr, n)` is `Malloc`.
- **The order problem.** A block made before `Install()` (or by a library the routing does not reach) and freed after goes to plain `free`, which is right because routed blocks are plain libc blocks; the free is reported without a matching alloc. So the live-byte counters, total and per source, subtract **saturating at zero** (a CAS loop) and never wrap. The same holds for a block made inside a `ScopedAllocCountersPause` and freed outside it (below); a block made outside and freed inside stays in the live bytes, since its free is not counted either (its entry of the block-tag table is erased all the same).
- **One read point.** Every counted allocation passes `RecordAlloc(source, ptr, size, usable)` and every free `RecordFree(source, ptr, usable)` in `AllocTracker.cpp`, whatever its source. The totals, the thread counters, the zone statistics, the no-alloc violations and the backends (Tracy, Chrome) see routed blocks exactly like global new, so a zone's allocation count includes them. `RecordAlloc` is where the thread's `MemTag` (`Memory/CLAUDE.md`, "Memory tags") is read, so a block of any source is counted under the tag in force; it must not allocate or lock.
- `AllocTracker::GetSourceTotals(source)`: `count`, `bytes`, `frees`, `liveBytes` of one source; `GetTotals()` is their sum.

## Counters

- `AllocTracker` (static, `constinit`): totals (count, bytes asked, frees, live usable bytes and their peak, the largest allocation, no-alloc violations) as relaxed atomics; per thread a monotonic count and bytes in a `constinit thread_local`, read by `ProfileZone` at both ends, so a zone's allocations are inclusive of inner zones and never another thread's. Per-site totals go into a fixed lock-free table (`kZoneSlots`), with or without a backend, so a window-less tool reads allocations per zone without a trace. A re-entrancy guard keeps a backend's `OnAlloc` that allocates (Tracy) from being counted twice. **Nothing on the allocation path allocates, locks or logs.**
- **Pause.** `ScopedAllocCountersPause` (`AllocTracker.h`, like `ScopedRenderCountersPause`) keeps the allocations and frees **of the calling thread** out of every count while it is alive: totals, source slots, thread counters, zone statistics, peak, largest and no-alloc violations. A depth counter in the thread state, so scopes nest. The backend (Tracy, Chrome) is still told, so its memory events stay balanced. Used by the profiler's own views (`Profiler/CLAUDE.md`); a no-op without the option.
- `BE_PROFILE_NO_ALLOC(name)`: allocations of the thread inside the scope are counted as violations and reported by one `LOG_WARNING` when the scope ends (name, file, line); never an abort.
- **No header in front of a block** (`AllocPlatform.h`): `malloc` / `posix_memalign` / `free` with `malloc_size` (macOS) or `malloc_usable_size`, and on Windows `_aligned_malloc` for every block, so whichever delete frees a block is right. That keeps EASTL's aligned `new[]` freed by a plain `delete[]` harmless (`Utils/EASTLOperators.cpp`); on Windows it holds in every build, because the MSVC runtime's own plain `delete` is `free`, which the Debug CRT's heap rejects for an aligned block. The cost: every plain Windows block carries the CRT's aligned header. `UsableSize` reads the release CRT's block header: valid only under the option over the release CRT; the Debug CRT with the option is refused at compile time (`AllocPlatform.cpp`).

## Tags

`GetTagTotals(tag)` / `GetTagTotals()` give `count`, `bytes`, `frees`, `liveBytes`, `peakLiveBytes` per `MemTag`; over the tags `count`, `bytes` and `frees` add up to `GetTotals()`.

- **In `frame_stats`:** a top-level `tags` array of `{tag, liveBytes, peakLiveBytes, count, frees}` in the order of the enum (the totals since the start, the peak is the running one), and in each frame `tagAllocCount` (the allocations of that frame) and `tagLiveBytes` (at its end) as objects keyed by tag name. Without `ENGINE_PROFILER_ALLOC` every number is 0.

- **Limits of a tag:** it is the allocating thread's (`Memory/CLAUDE.md`, "Memory tags"), so a task that runs on another thread counts under that thread's tag and a coroutine that resumes elsewhere never holds a scope. A library's block (pugixml, ImGui, SDL, stb) is tagged by the scope in force when the library allocated, not by the library. `EngineAllocator` carries no tag; its EASTL name goes to the table of "Names" below.
- **Why a table.** A free has no tag of its own and blocks carry no header (above), so `RecordAlloc` registers the block's address and tag in a fixed open-addressing table (`kBlockTableSize` slots of BSS, profile build only) and `RecordFree` finds, erases and subtracts from that tag. A key is published after its tag byte; an erased slot is a tombstone an insert may reuse and a lookup passes. Nothing allocates, locks or logs.
- **Untagged blocks are not registered:** a free that finds no entry is subtracted from `Untagged`, which is also where a block made before the tracker lands (saturating at zero like every live counter).
- **A block the table cannot hold** (no slot within `kBlockProbeLimit` probes) is counted once in `untrackedBlocks` and wholly under `Untagged`, so the totals stay right and only that block's split is lost. Tombstones are reclaimed only by reuse.
- **A `Realloc` that moves** reports the old block's free after libc has moved it: another thread can take the freed address and register it first, and then the free erases that entry (its own free lands in `Untagged`). The window is narrow and costs one block's split, never a total.

## Names

`AllocTracker::GetNameStats()` answers the allocations per **zone x name** (`AllocSiteTable`: count and bytes), `zone_allocs` as `names`, the Chrome trace as `otherData.allocNames`. It finds which container allocates inside a zone, which the per-zone totals cannot say.

- **Self, not inclusive:** an allocation is charged to the innermost open zone of its thread (`ProfileZone` keeps a thread-local current zone, only with the option), unlike `zones`, which add the inner zones. Outside every zone the row has a null zone (`(no zone)`); a plain `operator new` has a null name (`""`).
- **The name comes from EASTL:** the allocator name of the container (`"EASTL vector"`, `"EASTL basic_string"`, ...) or what `get_allocator().set_name("...")` gave. The engine's containers are on `EngineAllocator`, whose `allocate` hands it to `AllocTracker::SetNextName` right before its `operator new[]`; an `eastl::` container on EASTL's own allocator does the same through `EASTLOperators.cpp`. `RecordAlloc` takes and clears it, so a name belongs to one allocation. A plain `operator new` has none.
- **A library's block without an EASTL name** is recorded under the literal of its `AllocSource` (`AllocTracker::GetSourceName`: `pugixml`, `ImGui`, `SDL`, `stb`, `dr_libs`), so global `new`, each container name and each library are apart rows. One literal per source, because names are keyed by pointer.
- **Keyed by pointer**, merged by text on read: no hashing on the allocation path. A name therefore must be a string literal or live as long as the program; a `set_name` of a temporary buffer dangles. Zones and names are interned into pointer tables, a pair into a packed slot; when a table is full the allocation is dropped and counted (`GetNameOverflow()`), never blocked.
- **The profile build defines `EASTL_NAME_ENABLED=1` and `EASTL_DEBUGPARAMS_LEVEL=1`** (`cmake/BagiPlatformOptions.cmake`): macOS and Linux never define `_DEBUG`, so EASTL passes no names there otherwise. The definitions are global because `eastl::allocator` gets a `mpName` member (an ODR break if the engine and a test disagree). Level 2 would add file and line, but the default allocator passes its own `allocator.h` line, so they say nothing; the call site is the zone, and `set_name` where a zone is too coarse.
- The Tracy backend does not get the names (`IProfilerBackend::OnAlloc` has no name parameter).

## Sizes

`AllocTracker::GetSizeStats()` answers a histogram of the allocation sizes per zone (`AllocSiteTable`, the same `Add` as the names): bucket k holds the sizes in (2^(k-1), 2^k], bucket 0 holds 0 and 1, and the last of `kSizeBuckets` (26) everything above 16 MiB, since a block that large is a rare asset buffer whose size `largest` already gives. It tells what a zone allocates with (many 32-byte nodes, a few large buffers), which a pool is sized by.

- **Self-charged** to the innermost zone like `names`, not inclusive like `zones`; an allocation outside every zone is the row with a null zone. An inclusive histogram would need per-thread bucket copies read at both ends of every zone.
- **Fixed BSS**, `kZonePointers + 1` rows indexed by the interned zone index (row 0 the null zone), 64-bit counters, about 210 KB; `Add` still never allocates, locks or logs. Counted whenever the zone fits the zone pointer table, even when the name or the pair table is full; a full zone table drops the allocation into `GetNameOverflow()`.
- `ResetZoneStats` zeroes it with the names. Empty without `ENGINE_PROFILER_ALLOC`.

## Invisible

Not counted, by design or reach: libc internals (`fopen`, `opendir`, `printf` with `%f`, `tzset`), the Vulkan driver, Tracy's own memory, the browser's JavaScript side, a game's own copy of a library with its malloc (a second stb, dr_libs or pugixml; a game that uses pugixml or ImGui before the static initializer), and anything allocated before the static initializer ran. A newly visible steady-frame allocation in a game after `Update engine` is a finding for the game's own allocation test, not a regression of this tracker. An allocation made with a plain `operator new` has no EASTL name, so it shows in `names` only under the null name; a plain `malloc` is not counted at all.
