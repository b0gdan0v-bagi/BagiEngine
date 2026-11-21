# The allocation tracker sees only operator new: pugixml, imgui, SDL, stb and libc allocate unseen

- Version: 0.6.9
- Entry: alloc-tracking-complete
- Date: 2026-11-20
- Bump: patch

## Added
- `AllocRouting` (`BECore/Profiler/Alloc/AllocRouting.h`): `Malloc` / `Calloc` / `Realloc` / `Free` that report to `AllocTracker` under an `AllocSource` in the profile build (`ENGINE_PROFILER_ALLOC`) and are the plain C functions otherwise. pugixml, ImGui and SDL are pointed at them before `main`; stb and dr_libs call them at their include sites.
  Adapt: nothing; a game that vendors its own C library can route it with `AllocRouting` and an `AllocSource`.
- `AllocSource` (`GlobalNew`, `Pugixml`, `ImGui`, `SDL`, `Stb`, `DrLibs`) and per-source totals `AllocSourceTotals` in `AllocTracker`; `AllocPlatform::LibcUsableSize`.
  Adapt: nothing.

## Changed
- In the profile build the allocation counters, zone statistics and no-alloc violations now include the routed libraries, so a scene may show more allocations per frame than before (for example SDL's software present). `Profiler/Alloc/CLAUDE.md` lists what stays invisible.
  Adapt: re-check a game's zero-allocation zones in `macos-profile`; a new count there is a real allocation that was hidden before.
