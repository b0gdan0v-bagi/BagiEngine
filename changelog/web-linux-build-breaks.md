# Engine 0.8.4 does not build for the web and for Linux: TaskMemory alignment assert, SessionRecorder ofstream::open(string_view)

- Version: 0.11.2
- Entry: web-linux-build-breaks
- Date: 2026-11-23
- Bump: patch

## Added
- `TaskMemory::kBlockAlignment` in `TaskSystem/TaskMemory.h`: the alignment of every task block, the default `operator new` alignment of the target (16 on 64-bit, 8 on wasm32).

## Changed
- The engine compiles again for the web (Emscripten, wasm32) and for Linux with libstdc++: `TaskMemory` asserts the target's own alignment instead of 16, and `SessionRecorder` opens its file from a null-terminated copy of the path.
