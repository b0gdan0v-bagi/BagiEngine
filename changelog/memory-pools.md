# Memory: no fixed-size pools for small frequent objects (nodes, events, tokens, tasks)

- Version: 0.6.12
- Entry: memory-pools
- Date: 2026-11-20
- Bump: patch

## Added
- `BECore/Memory/`: block pools for small objects of one size: `BlockPool` (debug checks, ASan poisoning), `ConcurrentBlockPool` (per-thread lists over a locked central pool) and `SingleThreadBlockPool`. A class opts in with `BE_POOLED(T)` or `BE_POOLED_SINGLE_THREAD(T)` in the public section of its body (`Memory/Pooled.h`); `new T`, `MakeIntrusive<T>` and the `delete this` of the RefCounted bases then use the pool with no change at a call site. `Memory/CLAUDE.md` gives the rule for opting a type in.
  Adapt: nothing; a game's own small, frequently created class may opt in after measuring it.
- `PoolStats`: counters of every block pool (block size, live, peak, pages, allocations), read through `GetPoolStats()`; `FrameStats` has `poolLiveBlocks` and `poolPageBytes`, and the control method `frame_stats` answers them and a `pools` list.
  Adapt: nothing; a tool that reads `frame_stats` may show the new fields.

## Changed
- `SceneNode`, `TransformComponent`, `SpriteRendererComponent`, `TweenHandle`, `UIElement` and `UITextComponent` are allocated from single-thread pools, and `CancellationToken`'s state from a concurrent pool. A class derived from a pooled one keeps using the heap (the pool is told apart by size). A class-scope `operator new` hides the global ones: a `nothrow` or aligned `new` of these types no longer compiles.
  Adapt: create and destroy these objects on the main thread, as before (the Debug build asserts it); a game class derived from them needs nothing, or `BE_POOLED_SINGLE_THREAD(Derived)` to get a pool of its own.
