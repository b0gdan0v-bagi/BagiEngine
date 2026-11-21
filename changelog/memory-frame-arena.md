# Memory: no per-thread frame arena reset after the present

- Version: 0.7.4
- Entry: memory-frame-arena
- Date: 2026-11-20
- Bump: patch

## Added
- `BECore/Memory/FrameArena.h`, `FrameAllocator.h`: a per-thread frame arena (`FrameArena::Get()`) reset after the present, and `FrameVector<T>` on it, for memory handed on or kept until the end of the frame. The Debug build stamps each `FrameAllocator` with its frame and thread and asserts on a use in a later frame or on another thread. `Memory/CLAUDE.md` ("Frame arena") gives the rule for tasks: a task that can outlive the frame never uses it.
  Adapt: nothing; a game may use `FrameVector` for per-frame lists it hands to the renderer.

## Changed
- UI layout and text layout take their temporaries from the scratch arena, and a `UIElement`'s box mesh from the frame arena: a steady UI frame allocates less (relayout 24 to 0, text 10 to 4 allocations per frame). `UIBoxMesh`'s `Append*` functions take the vertex and index lists of the caller's allocator.
  Adapt: nothing, unless a game calls `UIBoxMesh::Append*` directly: pass `FrameVector` lists.
