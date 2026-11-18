# No computed values in data: a field is a literal, never read from a context, for any value type

- Version: 0.1.6
- Entry: data-values-core
- Date: 2026-11-16
- Bump: patch

## Added
- `BECore/Values/`: `Value<T>`, a field that is a literal or a slot of a context (`Const`, `Slot`; `Program` is reserved and refused at load), `Get(const Context&)` inline with no indirect call. The value types are the trivially copyable reflected ones, `PoolString`, enums and the math types (`ValueType.h`).
- `Context` / `ContextDecl`: levels that point at reflected structs the game already has (nothing copied); a path such as `caster.position.x` is linked at load to (level, offset, type), and an unknown path or a type mismatch is a load error that names the document, the field, the path and the fields the type has.
- Content: `<damage value="12"/>` and `<damage from="caster.attack"/>`; the plain attribute `damage="12"` keeps loading, so no migration. `<context>` declares the levels in the document (list form in every format, `<context caster="UnitStats"/>` in text formats); `MakeFromConfig` opens the link scope with the config's name. XML, binary and JSON read and write `Value<T>`.
- Versions per level for caching: `Context::Set(level, object, version)`, `VersionKey`, `Value::Reads`, `ValueDependencies`.
- `IDeserializer::AttributeNames` and `IsScalarMember` (defaults keep every existing reader as it was).
- Opaque value types (`Vec2`, `Vec3`, `Color`, ...) get an identity (size and name hash) in `TypeDesc`, so `SameType` and compiled paths tell them apart.
- Benchmark `ValueBench` and the `/perf` entry `value-get` (gate off until a baseline exists).
