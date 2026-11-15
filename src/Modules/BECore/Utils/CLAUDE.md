# Utils — callables and small helpers

## The callable types

| Type | Owns | Copy | Use for |
|------|------|------|---------|
| `MoveOnlyFunction<R(Args...), InlineBytes>` | yes | no, `noexcept` move | a callback that is stored (a task, a handler, a hook) |
| `FixedFunction<InlineBytes, R(Args...)>` | yes | no, `noexcept` move | a stored callback that must never allocate |
| `FunctionRef<R(Args...)>` | no | trivial copy (two pointers) | a parameter that is called only during the call |
| `ScopeGuard` | by value | no | code that runs when a scope ends; a template, no type erasure |

- **`std::function` and `eastl::function` are not used** (`BagiTypesCheck`, rule `function`): both need a copyable callable, so a move-only capture (`UniquePtr`, another function) forces a `SharedPtr` made only to copy it. A `std::function` can still be the source of a conversion.
- **The buffer.** `MoveOnlyFunction` keeps a callable inside when it fits (size, pointer alignment, `noexcept` move; the trait `kFunctionFitsInline<T, InlineBytes>` says so) and otherwise takes one allocation through the global `operator new`, so `AllocTracker` counts it per zone. The default `kDefaultFunctionInlineBytes` makes one function one cache line with its two pointers and holds the captures of tasks and handlers; a callable that holds a function of the same type is above it and takes the heap path. A function that holds a function must pick its buffer on purpose.
- **`FixedFunction` is not `eastl::fixed_function`:** that one copies its callable, so it cannot hold a move-only capture. Both of ours are one implementation (`detail::FunctionImpl`); `FixedFunction` has no heap path, and a callable that does not fit is not accepted (`std::is_constructible_v` says false, and the compiler's note names the callable).
- **`operator()` is `const`** and calls the stored callable as non-const, like `std::function`: a `mutable` lambda keeps its state, and a function reached through a `const` reference can be called. Calling an empty function is an `ASSERT`ed contract violation; test `explicit operator bool` first when it may be empty.
- **A moved-from function is empty**, and the callable is destroyed exactly once (moved-from, `Reset`, overwritten by assignment, destroyed with its holder, inline or on the heap). Construction from another function type moves the callable in; storage is never shared.
- **Not `std::move_only_function`:** it promises no buffer size, so the allocation of a capture could not be reasoned about, and older libc++ versions (the web build's maybe) lack it.
- **`FunctionRef` is never stored** (a member, a captured value, a returned one): it points at the callable, which a temporary lambda keeps only to the end of the full expression that calls. A `FunctionRef` built from a `FunctionRef` is a plain copy.
- **`ScopeGuard` runs its callable in the destructor** and cannot be copied or dismissed; `ScopeGuard guard([&] { ... });` deduces the type.
- `tests/BECore/CallablesBenchTest.cpp` times the types against `eastl::function`; `BE_CALLABLES_BENCH=<iterations>` prints the numbers. It asserts nothing on time and is not a member of the perf set (`tools/perf/CLAUDE.md`).
