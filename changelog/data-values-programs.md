# Computed values: no formulas in data, compiled into fast typed programs for any value type

- Version: 0.2.2
- Entry: data-values-programs
- Date: 2026-11-16
- Bump: patch

## Added
- `Value<T>` of the kind `Program`: a field written `expr="caster.attack * (100 + ability.level * 10) / 100"` is parsed, type-checked against the context, constant-folded and compiled at load into a flat typed program (`Values/Programs/`); `Value<T>::Evaluate(context)` runs it with no allocation and no virtual call. XML, binary and JSON load and save `expr`.
- Program types: the scalars, `Vec2`, `Vec3`, `Vec4`, `IVec2`, `Quat`, `Color`, enums and `PoolString` (equality); components (`pos.y`) and constructors (`vec3(a, b, c)`), mixed pairs (`Vec3 * float`, `Quat * Vec3`), `lerp`, `clamp`, `min`, `max`, `select`, `dot`, `length`, `normalize`; a missing operator is a load error naming the file, the field and the place in the text.
- Tables: inline `at(i, e0, e1, ...)` and named `ByLevel('Name', i)` (0-based) over a `ValueTableSet` provider the game puts on the `ValueLinkScope`.
- The tree form: reflected node classes `CalcNode` (`CalcNumber`, `CalcRead`, `CalcBinary`, `CalcCall`, `CalcByLevel`, ...; `Values/Nodes/`) loaded from the key `tree` of a `Value` in the engine's factory form, compiled by the same code and saved as text.
- `CompileValue<T>` (`Programs/ProgramCompiler.h`): compile a text to a `Value<T>` from code, the entry point for `data-bindings`; `CompileOptions::fuse` turns the peephole superinstructions (`slot * k + b`, `lerp` of slots, a table over a slot) on or off.
- `CachedValue<T>`: keeps a program's last result while the version key of the levels it reads holds; `Context::IsVersioned(levels)`.
- `TypeDesc::enumBytes`: the size of an enum, part of its identity in `SameType`.
- Benchmarks of `int32_t` and `Vec3` formulas (plain C++, virtual tree, program fused and unfused, cached) in the `value-get` perf set.

## Changed
- `Context::VersionKey` also folds the address of each level's object and counter: another object at a level gives another key even when the numbers agree, and a context rebuilt each frame over the same objects gives the same key.
- `Lerp(Color)` returns 0 in every channel for a NaN `t` instead of undefined behaviour.
- `Value<T>::Reads()` of a `Program` returns the levels the program reads.
