# Engine containers use EASTL's default allocator: no memory tags per subsystem, no single point to route or account memory

- Version: 0.7.0
- Entry: engine-allocator-tags
- Date: 2026-11-20
- Bump: minor

## Added
- `BECore/Memory/MemTag.h`: memory tags (`MemTag::Core`, `UI`, `Text`, `Scene`, `Resource`, `Audio`, `Network`, `Game`). `BE_MEM_TAG(UI)` counts every allocation the thread makes until the end of the scope under that tag, whoever makes it; scopes nest and the tag is the thread's. The engine tags its own subsystem entry points and the callbacks of `IGame` (`Game`). Only with `ENGINE_PROFILER_ALLOC`; otherwise the scope costs nothing.
  Adapt: nothing; a game may tag its own coarse entry points (never inside a coroutine).
- `BECore/Memory/EngineAllocator.h`: the EASTL allocator of the engine's owning containers, as large as `eastl::allocator` in every build; in the profile build it reports the EASTL name to `AllocTracker` (`GetNameTotals`).
  Adapt: nothing.
- Statistics per tag: `AllocTracker::GetTagTotals()`, `FrameStats::tagAllocCount` / `tagLiveBytes`, and in the control method `frame_stats` the per-frame `tagAllocCount` / `tagLiveBytes` by tag name and a top-level `tags` list.
  Adapt: nothing; a tool that reads `frame_stats` may show the new fields.

## Breaking
- `String`, `FixedString`, `Vector`, `SmallVector`, `FixedVector`, `HashMap`, `Map`, `HashSet`, `Set`, `FixedHashMap`, `FlatMap` and `FlatSet` now take `EngineAllocator`, so they are no longer the `eastl::` type of the same name: an `eastl::string` or `eastl::vector` spelled by hand does not bind to an API that takes the alias, does not move into it, and `==` between the two is ambiguous.
  Adapt: spell the engine aliases instead of `eastl::` types; where a value of the old type is unavoidable, copy it by range (`String{view}`) or compare views (`StringView{a} == StringView{b}`).
