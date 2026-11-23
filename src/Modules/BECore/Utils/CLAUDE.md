# Utils — callables and small helpers

## The callable types

| Type | Owns | Copy | Use for |
|------|------|------|---------|
| `MoveOnlyFunction<R(Args...), InlineBytes>` | yes | no, `noexcept` move | a callback that is stored (a task, a handler, a hook) |
| `FixedFunction<InlineBytes, R(Args...)>` | yes | no, `noexcept` move | a stored callback that must never allocate |
| `FunctionRef<R(Args...)>` | no | trivial copy (two pointers) | a parameter that is called only during the call |
| `ScopeGuard` | by value | no | code that runs when a scope ends; a template, no type erasure |

- **`std::function` and `eastl::function`: new code does not use them** (`BagiTypesCheck`, rule `function`; the engine has none left, a test that needs copies marks it `//-NoStd`): both need a copyable callable, so a move-only capture (`UniquePtr`, another function) forces a `SharedPtr` made only to copy it. A `std::function` can still be the source of a conversion.
- **The buffer.** `MoveOnlyFunction` keeps a callable inside when it fits (size, pointer alignment, `noexcept` move; the trait `kFunctionFitsInline<T, InlineBytes>` says so) and otherwise takes one allocation through the global `operator new`, so `AllocTracker` counts it per zone. The default `kDefaultFunctionInlineBytes` makes one function one cache line with its two pointers and holds the captures of tasks and handlers; a callable that holds a function of the same type is above it and takes the heap path. A function that holds a function must pick its buffer on purpose.
- **The heap path has its own row in `zone_allocs`:** it names the allocation `MoveOnlyFunction heap` (`detail::NameHeapCallable()`, out of line so the header includes no profiler), so `names` shows its count per zone. Measured in `macos-profile` over the whole run of the `BagiStress` scenes `sprites`, `entities` and `ui` and over the scene, UI, widget and component suites of `BagiEngineTests`: the row never appears, zero heap-path allocations, so none per steady frame. Every callback these paths store fits the buffer, and a pool of size classes for the heap path is not worth building; it would be once a scene shows more than one heap-path allocation per steady frame.
- **`FixedFunction` is not `eastl::fixed_function`:** that one copies its callable, so it cannot hold a move-only capture. Both of ours are one implementation (`detail::FunctionImpl`); `FixedFunction` has no heap path, and a callable that does not fit is not accepted (`std::is_constructible_v` says false, and the compiler's note names the callable).
- **`operator()` is `const`** and calls the stored callable as non-const, like `std::function`: a `mutable` lambda keeps its state, and a function reached through a `const` reference can be called. Calling an empty function is an `ASSERT`ed contract violation; test `explicit operator bool` first when it may be empty.
- **A moved-from function is empty**, and the callable is destroyed exactly once (moved-from, `Reset`, overwritten by assignment, destroyed with its holder, inline or on the heap). `Reset` and assignment put the new state (empty, or the new callable) in before the old callable is destroyed, so its destructor sees a valid function and may assign it. Construction from another function type moves the callable in; storage is never shared.
- **Not `std::move_only_function`:** it promises no buffer size, so the allocation of a capture could not be reasoned about, and older libc++ versions (the web build's maybe) lack it.
- **`FunctionRef` is never stored** (a member, a captured value, a returned one): it points at the callable, which a temporary lambda keeps only to the end of the full expression that calls. A `FunctionRef` built from a `FunctionRef` is a plain copy.
- **`ScopeGuard` runs its callable in the destructor** and cannot be copied or dismissed; `ScopeGuard guard([&] { ... });` deduces the type.
- `tests/BECore/CallablesBenchTest.cpp` times the types against `eastl::function`; `BE_CALLABLES_BENCH=<iterations>` prints the numbers. It asserts nothing on time and is not a member of the perf set (`tools/perf/CLAUDE.md`).

## EASTL operators

`EASTLOperators.cpp` defines the `operator new` overloads with EASTL's debug parameters (name, flags, file, line) and forwards to the plain and the aligned global operators. EASTL allocates an over-aligned array with the aligned `new[]` but frees it with the plain `delete[]`, so the pair must be safe: on Windows the replacement of the global operators (`Profiler/Alloc/GlobalOperatorNew.cpp`, in every build but the ASan one) frees every block through `_aligned_free`, where the MSVC runtime's `delete` is `free` and corrupts the Debug CRT's heap; on macOS and Linux an aligned block is `posix_memalign` memory, which `free()` takes. `tests/BECore/EastlAlignedAllocTest.cpp` holds it.

## The engine version

`EngineVersion.h` holds `MAJOR.MINOR.PATCH` of the engine (`0.MINOR.PATCH` while it is young: a breaking change raises MINOR, anything else PATCH).

- A game may rely on the macros `BE_ENGINE_VERSION_MAJOR/MINOR/PATCH` and `BE_ENGINE_VERSION_AT_LEAST(a, b, c)` in `#if` (the constants cannot be used there), on `BE_ENGINE_VERSION_STRING`, and on the constants `kEngineVersionMajor/Minor/Patch` and `kEngineVersion`. The string is built from the macros, so it cannot drift from the numbers.
- The numbers are changed only by `tools/changelog/changelog.py` (run by `todo-merge` with the changelog fragment of the merged entry), which rewrites the three `#define` lines; a developer never edits them.
