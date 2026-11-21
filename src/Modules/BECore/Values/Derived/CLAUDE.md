# Derived: a value built from a context, rebuilt only when an input moved

`Derived<T>` (`Derived.h`, header only) is what a screen holds instead of a version key of its own: a text, a list or a layout struct that is a function of a `Context` (`../CLAUDE.md`, "Versions"). The reads are declared **once**, at construction, as `DerivedReads`; each frame the screen calls `Get(context, build)`.

## The declaration

`DerivedReads` is a `LevelMask` plus up to `kMaxVersionSources` (4) plain `const uint64_t*` counters that belong to no level (the table of `UIVars`). The mask comes from `Add(const Value<T>&)` (the level a `Slot` reads, nothing for a `Const`), `AddLevels(mask)` (a `ValueDependencies`), `AddLevel(index)` or `AddLevel(declaration, "name")`, which is a `LinkError` (`unknown_level`, the declared levels and a `did you mean`) so a typo shows when the screen is made. A source counter must outlive the value and only grow; a fifth source is an `ASSERT` and `AddSource` returns false, never a silent drop. Nothing declared: the value is built once (until `Invalidate` / `Reset`).

## A call

In this order: no declaration and valid is a hit; a declared level that is not `IsVersioned` (no object or no counter in this context) rebuilds on every call and a debug build logs it once per instance; else the source counters are compared one by one (not hashed), then `Context::VersionKey(mask)` (skipped for an empty mask): equal is a hit. A hit is inline loads and compares: no allocation, no virtual or indirect call. A context rebuilt each frame over the same objects and counters hits.

The key and the counters are stored **after** the build, so a builder that bumps a counter it declared does not rebuild forever; the price is that its own bump is invisible to the value.

## The builder

A parameter of `Get` (a `FunctionRef`, valid for the call), not a stored callback: the value stays movable and a lambda over `this` cannot dangle. It fills `T& out` in place and gets the last result, so a `String` or `Vector` it clears keeps its capacity and a rebuild that fits allocates nothing: reserve once, `clear()`, append.

Where asserts are on, the builder gets `Context::Restricted(mask)`: a copy without the undeclared levels, so a `Value::Get` of one fails the `ASSERT` for a null level instead of leaving a stale screen. **It cannot see** a read through anything but the `Context` it was given (a captured pointer to the object, a global); keep every read of a builder in its declaration.

## Traps

A counter nobody bumps is a value that never rebuilds; a counter that goes down can fold back to an earlier key; an object replaced without a bump is the owner's trap (`../CLAUDE.md`, "Versions"). Not thread-safe; movable, not copyable.

## When `CachedValue` is the better tool

A result that is a `ValueType` of one formula: `CachedValue` runs the program and keeps the result. `Derived` is for anything else, or for a result of several values and C++.
