# Values: data that is a literal or a read of a context

`Value<T>` (`Value.h`) is a field type for content: `damage="12"` is a literal, `<damage from="caster.attack"/>` reads the field `attack` of an object a game put into a context. A game changes a field from `int32_t` to `Value<int32_t>` and its content keeps loading. `T` is a `ValueType` (`ValueType.h`): a number, a bool, an enum, a `PoolString`, or a small trivially copyable object (`Vec3`, `Color`, a game's own `Point`). A `String`, a list, a map, an optional and a pointer are not value types: they own memory, so a slot of them is not a plain read.

## Kinds

`ValueKind`: `Const` (the literal), `Slot` (a field of a context object), `Program` (reserved for formulas; `Get` asserts, the loader refuses it). The literal is a plain member beside the slot, not a union, so every `Value` is a valid literal of its type. `Get(const Context&)` returns `const T&` into the literal or into the context object: copy out what must outlive the object. It is a branch and an addition, with no lookup and no allocation. A `Value<T>` converts implicitly from `T`.

## Context

`Context` (`Context.h`) is a fixed array of `kMaxContextLevels` pointers, one per level; each points to a reflected object of the type `ContextDecl` gives that level. Nothing is copied into it: the game writes its plain fields and a value reads them where they are. The objects and the declaration outlive the context. A null level is an `ASSERT` in `Get`, not a branch. An object of a derived type is stored at its base subobject, because the offsets are those of the declared type (`Context::Set` does it when it knows the declaration).

`ContextDecl` holds the levels, each a name and a registered `TypeInfo` (`Add(name, typeName)` checks the name against `TypeRegistry`; `Add<T>(name)` is for code).

## Paths and linking

A path is `level.property.path` (`caster.position.x`). `LinkSlot` splits the level off, compiles the rest with `CompilePath` against the level's type and turns it into `SlotRef{level, offset}` once, at load. The path must be **fully fixed**: a list element, map value, optional or pointee needs the live object and is refused (`not_fixed`). The field's type must be the type asked for under `SameType`: no numeric conversion (an `int` field into `Value<float>` is refused), because a conversion is a cost on the common path and a hidden rule in data. Every refusal is a `LinkError` whose message names the path, the field and what is wrong: the declared levels with a `did you mean`, the fields of the type, both types of a mismatch.

`SameType` can tell `Vec2` from `Vec3` or `Rect` from `Vec4` only because an `Opaque` descriptor carries the size and a hash of the type's name (`Reflection/CLAUDE.md`, `TypeDesc`). Without it any opaque field would link as any opaque value and `Get` would read the wrong bytes: the identity is what makes a `Slot` type safe.

## Loading

`ValueAccessor.h` is `Ser::DataAccessor<Value<T>>`; `Value.h` includes it, so a generated `.gen.hpp` that names a `Value` field sees it. Grammar: the plain attribute `damage="12"` and `<damage value="12"/>` are a `Const`; `<damage from="caster.attack"/>` is a `Slot`. An object or opaque `T` keeps its own form as the literal (`<pos x=".." y=".." z=".."/>`) and `<pos from="caster.position"/>` reads. `value` beside `from` is an error. `kind` (`Const` or `Slot`) is an optional key that XML and binary accept; no writer emits it. The keys `kind`, `from` and `value` are reserved: an object type used as a `Value<T>` literal (a game's own `Point`) cannot have an attribute of one of these names. `Save` writes a scalar `Const` as the plain attribute, so a file written before the field became a `Value` loads and saves as before: **no migration step and no `kContentVersion` bump is needed to turn a field into a `Value`**. Binary has the same shapes; `Program` or an unknown `kind` is an error in either format. A load that fails leaves the field as it was and reports under the field's own path and line (`IDeserializer::ReportError`). `kind`, `from` and `value` are the known keys for strict mode and the diagnostics of `XmlDeserializer`; any other key is an unknown key those diagnostics report.

`ValueLinkScope` (RAII) says which `ContextDecl` the `from` of a load is linked against. It is a stack **per thread**: the accessor has no parameter to carry a declaration through the generated code, and two threads loading at once must never see each other's context. A `from` read with no scope is a load error. In this stage the caller of a load opens the scope from code.

## Not here

The `<context>` element in content, the config loader opening a scope, versions of levels (a key for "did anything I read change"), the benchmark, formulas (`Program`) and the explanation of a value (why it is what it is): later entries (`data-values-*`). Reading or writing a `Value` field by name through the control API treats it as an opaque value, not a leaf.
