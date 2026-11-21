# EASTL's allocation names and file:line are dropped: the allocation profiler sees only the zone

- Version: 0.8.0
- Entry: eastl-alloc-debug-params
- Date: 2026-11-20
- Bump: minor

## Added
- Allocations per innermost zone x allocation name (`AllocTracker::GetNameStats()`, `GetNameOverflow()`): the control method `zone_allocs` answers them as `names`, the Chrome trace as `otherData.allocNames`. The name is the container's EASTL allocator name (`"EASTL vector"`, ...) or what `get_allocator().set_name("...")` gave, so a zone's allocations can be traced to a container. Profile build only (`ENGINE_PROFILER_ALLOC`).
  Adapt: nothing; a name given with `set_name` must be a literal or live as long as the program.

## Changed
- The profile build defines `EASTL_NAME_ENABLED=1` and `EASTL_DEBUGPARAMS_LEVEL=1` for every target (`cmake/BagiPlatformOptions.cmake`), so EASTL passes names on macOS and Linux too.
  Adapt: nothing for a game built through `cmake/BagiEngine.cmake`; a target compiled outside it must get the same definitions in the profile build (the allocator's size depends on them).

## Breaking
- `AllocTracker::OnNamedAlloc`, `GetNameTotals` and `AllocNameTotals` (from 0.7.0) are removed: `EngineAllocator` names its blocks through `AllocTracker::SetNextName`, the one path EASTL's own allocator uses, and the totals per name are rows of `GetNameStats()`.
  Adapt: sum the rows of `AllocTracker::GetNameStats()` whose `name` equals yours instead of calling `GetNameTotals(name)`.
