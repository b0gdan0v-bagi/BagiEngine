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
- **The order problem.** A block made before `Install()` (or by a library the routing does not reach) and freed after goes to plain `free`, which is right because routed blocks are plain libc blocks; the free is reported without a matching alloc. So the live-byte counters, total and per source, subtract **saturating at zero** (a CAS loop) and never wrap.
- **One read point.** Every counted allocation passes `RecordAlloc(source, ptr, size, usable)` and every free `RecordFree(source, ptr, usable)` in `AllocTracker.cpp`, whatever its source. The totals, the thread counters, the zone statistics, the no-alloc violations and the backends (Tracy, Chrome) see routed blocks exactly like global new, so a zone's allocation count includes them. `RecordAlloc` is where the thread-local scope tag of `engine-allocator-tags` is read; it must not allocate or lock.
- `AllocTracker::GetSourceTotals(source)`: `count`, `bytes`, `frees`, `liveBytes` of one source; `GetTotals()` is their sum.

## Counters

- `AllocTracker` (static, `constinit`): totals (count, bytes asked, frees, live usable bytes and their peak, the largest allocation, no-alloc violations) as relaxed atomics; per thread a monotonic count and bytes in a `constinit thread_local`, read by `ProfileZone` at both ends, so a zone's allocations are inclusive of inner zones and never another thread's. Per-site totals go into a fixed lock-free table (`kZoneSlots`), with or without a backend, so a window-less tool reads allocations per zone without a trace. A re-entrancy guard keeps a backend's `OnAlloc` that allocates (Tracy) from being counted twice. **Nothing on the allocation path allocates, locks or logs.**
- `BE_PROFILE_NO_ALLOC(name)`: allocations of the thread inside the scope are counted as violations and reported by one `LOG_WARNING` when the scope ends (name, file, line); never an abort.
- **No header in front of a block** (`AllocPlatform.h`): `malloc` / `posix_memalign` / `free` with `malloc_size` (macOS) or `malloc_usable_size`, and on Windows `_aligned_malloc` for every block, so whichever delete frees a block is right. That keeps EASTL's aligned `new[]` freed by a plain `delete[]` harmless (`Utils/EASTLOperators.cpp`); on Windows it holds in every build, because the MSVC runtime's own plain `delete` is `free`, which the Debug CRT's heap rejects for an aligned block. The cost: every plain Windows block carries the CRT's aligned header. `UsableSize` reads the release CRT's block header: valid only under the option in a release-runtime preset, never over the Debug CRT.

## Names

`AllocTracker::GetNameStats()` answers the allocations per **zone x name** (`AllocSiteTable`: count and bytes), `zone_allocs` as `names`, the Chrome trace as `otherData.allocNames`. It finds which container allocates inside a zone, which the per-zone totals cannot say.

- **Self, not inclusive:** an allocation is charged to the innermost open zone of its thread (`ProfileZone` keeps a thread-local current zone, only with the option), unlike `zones`, which add the inner zones. Outside every zone the row has a null zone (`(no zone)`); an allocation without a name has a null name (`""`).
- **The name comes from EASTL:** the allocator name of the container (`"EASTL vector"`, `"EASTL basic_string"`, ...) or what `get_allocator().set_name("...")` gave. `EASTLOperators.cpp` hands it to `AllocTracker::SetNextName` and `RecordAlloc` takes and clears it, so a name belongs to one allocation. A plain `operator new` has none.
- **Keyed by pointer**, merged by text on read: no hashing on the allocation path. A name therefore must be a string literal or live as long as the program; a `set_name` of a temporary buffer dangles. Zones and names are interned into pointer tables, a pair into a packed slot; when a table is full the allocation is dropped and counted (`GetNameOverflow()`), never blocked.
- **The profile build defines `EASTL_NAME_ENABLED=1` and `EASTL_DEBUGPARAMS_LEVEL=1`** (`cmake/BagiPlatformOptions.cmake`): macOS and Linux never define `_DEBUG`, so EASTL passes no names there otherwise. The definitions are global because `eastl::allocator` gets a `mpName` member (an ODR break if the engine and a test disagree). Level 2 would add file and line, but the default allocator passes its own `allocator.h` line, so they say nothing; the call site is the zone, and `set_name` where a zone is too coarse.
- The Tracy backend does not get the names (`IProfilerBackend::OnAlloc` has no name parameter).

## Invisible

Not counted, by design or reach: libc internals (`fopen`, `opendir`, `printf` with `%f`, `tzset`), the Vulkan driver, Tracy's own memory, the browser's JavaScript side, a game's own copy of a library with its malloc (a second stb, dr_libs or pugixml; a game that uses pugixml or ImGui before the static initializer), and anything allocated before the static initializer ran. A newly visible steady-frame allocation in a game after `Update engine` is a finding for the game's own allocation test, not a regression of this tracker. An allocation made with a plain `operator new` or `malloc` has no EASTL name, so it shows in `names` only under the null name.
