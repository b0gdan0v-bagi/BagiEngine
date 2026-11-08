---
paths:
  - "src/Modules/BECore/Reflection/**/*"
  - "CI/meta_generator/**/*"
  - "tests/BECore/Reflection*"
---

# Reflection System Guidelines

## Overview

Compile-time reflection with a Python code generator (`meta_generator`). `BE_CLASS` / `BE_EVENT` plus `BE_REFLECT_FIELD` markers produce `.gen.hpp` files with `Serialize()` (write), `Deserialize()` (read), field iteration and type metadata.

## Codegen (meta_generator)

- **Script:** `CI/meta_generator/meta_generator.py`; run automatically at configure time (so re-run `cmake --preset ...` after changing markers) by `add_reflection_target()` for the engine and `be_add_game_reflection()` for each game, both in `cmake/BagiEngine.cmake`.
- **Engine target config:** `CI/meta_generator/reflection_targets.json`, per target `source_dirs`, `scan_dirs`, `include_dirs`. Only `BECoreModule` has an entry, and its `scan_dirs` also cover SDL, Vulkan, Games, `src/Widgets` and `tests/BECore`. **A new directory with reflected engine classes must be added there.** The file lists engine directories only: it never names a game.
- **Game targets:** `be_add_game_reflection(<Target>)` passes the game's `src/` as `--source-dir`/`--include-dir` and `--reference-target BECoreModule`. Reference headers (the engine's) are parsed for factory bases, parents and `CORE_ENUM`s, but nothing is generated for them. A game has its own cache (`build/<preset>/.meta_cache/<Target>/`) and no entry in `reflection_targets.json`; its `#include` paths are computed from the game's `src/`, so `games/Match3/src/Match3/Match3Game.h` is included as `<Match3/Match3Game.h>`.
- **Manifest:** every run writes `--manifest` (`.meta_cache/<Target>/generated_files.txt`): the `.gen.hpp` files that run owns, including the ones that were up to date. CMake adds exactly those to the target; the engine and the games share `Generated/`, so it is never globbed.
- **Manual run:** `--target BECoreModule --project-root . --output-dir build/Generated --cache-dir build` (engine), or `--source-dir games/Match3/src --include-dir games/Match3/src --reference-target BECoreModule --project-root . --output-dir ... --cache-dir ...` (game). The cache is `metadata_cache.json` in `cache-dir`.
- Output goes to `build/<preset>/Generated/`. Never edit generated files.
- **Warnings:** the parser is regex based, so a marker it cannot turn into metadata is reported and never dropped silently. Every run prints one line per skipped marker to stderr: `path:line: warning: reflection: <what and why>` (the line is the marker's line in the file, as are the `line` values of classes, fields and methods in the cache; the compiler and the editors can follow it). The warnings are stored with the file in the cache (`FileMetadata.warnings`, `CACHE_VERSION`), so an unchanged file prints them again on every configure, not only the first time. Only the files of the target itself are reported (`source_dirs` and `scan_dirs`, not the reference dirs of another target: the engine's run owns the engine's warnings). The summary line says `N reflection warnings`; `--quiet` prints nothing. A warning never changes the exit code, because CMake stops the configure on a non-zero one; `--warnings-as-errors` (not passed by CMake) exits with 1 when there is any warning, also with `--quiet`. A `FACTORY_BASE` whose generated enum name (`ILogSink` -> `LogSinkType`) is already declared by a handwritten `CORE_ENUM` in the scanned sources gets a warning too. The real tree (engine, Match3, Chess) prints none: a new warning is a marker that does nothing, so rewrite the declaration or extend the parser.
- **Tests of the generator:** pytest, not part of ctest: `.venv/bin/python -m pytest CI/meta_generator/tests` (from the engine root; `pytest` is in `requirements.txt`). The files pin the parser construct by construct (`test_parser_classes.py`, `test_parser_fields.py`, `test_parser_methods.py`), the warnings (`test_warnings.py`) and the cache and command line (`test_cache_and_cli.py`; the `main()` cases skip without `jinja2`). A construct that is a limitation has a test that asserts its warning, so a parser change that supports it shows up as a failing test to update.
- **What the regex parser does not handle** (each one warns, except where marked "silent"):
  - Class forms: `class Foo final : ...`, an export macro (`class MY_API Foo`), `alignas(...)` and an attribute before the name (`class [[nodiscard]] Foo`), a template specialization (`struct Foo<int>`) are not recognised, so the `BE_CLASS` and every marker inside warn. A class template header (`template<typename T> struct Foo`) is fine. The first `BE_CLASS` / `BE_EVENT` in a class must name that class; a reflected class nested in a reflected class warns for the nested markers (reflect nested classes separately).
  - Fields: one declarator per `BE_REFLECT_FIELD` (`int a, b;` and `int a = 1, b = 2;` warn), no arrays (`float _m[16]`), bitfields or function pointers, and the declaration ends at its first `;` outside brackets. `static` / `constexpr` stay in the type (**silent**, such a field cannot be serialized), `mutable` and `[[attributes]]` are dropped.
  - Methods: `BE_FUNCTION` needs a return type (a constructor warns), is read by its name (`&Class::Name`), so templates, operators other than `operator()` and overloads that make `&Class::Name` ambiguous cannot be reflected (templates and operators warn, an overload is **silent** and fails to compile in the `.gen.hpp`). An inline body (`BE_FUNCTION virtual void DrawScreen() {}`) ends the declaration. A declaration without `;` that runs into the next marker warns once.
  - No preprocessor and no scoping: a class in an `#if 0` block or behind an undefined macro is generated like any other, a class declared in a function body is picked up (**silent**; the generated code cannot name it), `inline namespace v1` is a segment of the name (`A::v1::Foo`), and an unknown option of `BE_CLASS` (`FACTORY_BAS`) is ignored (**silent**).
  - Comments and the contents of string, character and raw string literals are blanked before parsing (newlines kept), so a marker in them is no marker and `"http://x"` does not eat the line.

## Architecture

Separate interfaces for writing and reading, sharing the structural base `IArchiveBase` (`BeginObject` / `EndObject`, `BeginArray` / `EndArray`, `BeginArrayElement` / `EndArrayElement`):

- **`ISerializer`**: `Write()`, `WriteAttribute()`. Implementations: `XmlSerializer`, `BinarySerializer`.
- **`IDeserializer`**: `Read()`, `ReadAttribute()`, errors via `HasErrors()` / `GetErrors()` / `ClearErrors()` (`DeserializeError`: `path`, `errorMessage`, `line`). Implementations: `XmlDeserializer`, `BinaryDeserializer`.
  Ask the engine for one instead of constructing `XmlDeserializer` yourself: `CoreManager::GetConfigManager().CreateDeserializer(name)` for a named config, `OpenDeserializer(virtualPath)` (`Reflection/OpenDeserializer.h`) for a file, whose format follows the extension (`.xml`, `.bin`).
- **`IPropertyVisitor`**: in-place field editing (ImGui inspector, `ImGuiPropertyVisitor`), reached through the generated `AcceptPropertyVisitor()`.

## Macros

| Macro | Purpose |
|-------|---------|
| `BE_CLASS(Name)` | Reflection + `Serialize` / `Deserialize` + `AcceptPropertyVisitor` + `Is<T>()` / `Cast<T>()` |
| `BE_CLASS(Name, FACTORY_BASE)` | Also generates the type enum and `AbstractFactory` registration for the hierarchy |
| `BE_EVENT(Name)` | Lightweight variant for events (`.claude/rules/reflection-events.md`) |
| `BE_REFLECT_FIELD` | Mark a field for serialization (same line as the field) |
| `BE_FUNCTION` | Mark a method for reflection |

## Serialization

The generated `Serialize` calls `Ser::Save(s, field, "name")` for every field, `Deserialize` calls `Ser::Load`. `Ser::Save` / `Ser::Load` (`Reflection/DataAccessor.h`) pick a `Ser::DataAccessor<T>` specialization:

| Type | Written as |
|------|-----------|
| arithmetic, `eastl::string`, `PoolString`, reflected enum (`CORE_ENUM`) | attribute (`WriteAttribute` / `ReadAttribute`) |
| type with `Serialize` / `Deserialize` | child object: `BeginObject(key)` + `Serialize` |
| `eastl::vector<T>` (and fixed arrays) | array; elements are objects or a `value` attribute |
| `eastl::vector<IntrusivePtr<Base>>` with a factory | array; each element has `type` (class name) and `enabled` attributes, created through `AbstractFactory`; `Initialize()` is called on loaded elements when present, and so is `bool Initialize(IDeserializer&)` (declared on the base, like `IWidget`'s) inside the element's own scope: `false` drops the element and reports `<TypeName> failed to initialize` |
| `IntrusivePtrAtomic<T>` / `IntrusivePtrNonAtomic<T>` | one child element; null is skipped |

**Strict values:** `IDeserializer::SetStrictValues(true)` (`XmlDeserializer` implements it; `BinaryDeserializer` has no text to check and keeps the default) makes the reads of a bool, the integer types, `float` and `double` check the whole text: a bool must be `true` / `false` / `1` / `0`, a number must be a number of the target type (spaces around it and a leading `+` are fine; `abc`, `12abc`, `0x10`, `1.5` for an integer, `-1` for an unsigned one, `65536` for a `uint16_t`, `nan` are not). A bad text is an error `Invalid boolean|integer|number value '<text>'` (with ` (out of range)` for a number that parses but does not fit) under the path of the field, `ReadAttribute` / `Read` return `false` and the field keeps its value; a missing attribute is still no error. It is off by default, on purpose: `XmlDeserializer` is lenient (a malformed number is 0, any other bool text is false) and callers like `UIDocument` reject a whole document on any error, so a default change would reject existing files. A caller that wants the checks (for example a config with a port, or the command-line and JSON sources) turns it on before `Deserialize`.

**Field naming:** a leading underscore is stripped for the serialized name (`_clearColor` → `clearColor`).

```cpp
struct Player {
    BE_CLASS(Player)
    BE_REFLECT_FIELD int32_t health = 100;      // same line as the marker
    BE_REFLECT_FIELD PoolString name;
    BE_REFLECT_FIELD Math::Color _clearColor;   // serialized as "clearColor"
    bool _internal = false;                     // no marker: not serialized
};

XmlSerializer serializer;
if (serializer.BeginObject("Player")) {
    player.Serialize(serializer);               // const
    serializer.EndObject();
}
serializer.SaveToFile("save.xml");

XmlDeserializer deserializer;
deserializer.LoadFromFile("save.xml");
if (deserializer.BeginObject("Player")) {
    player.Deserialize(deserializer);
    deserializer.EndObject();
}
if (deserializer.HasErrors()) {
    for (const auto& err : deserializer.GetErrors()) {
        LOG_ERROR("Deserialize error: {} - {} (line {})"_format(err.path, err.errorMessage, err.line));
    }
}
```

```xml
<Player health="100" name="Hero"><clearColor r="..." g="..." b="..." a="..."/></Player>
```

### Manual arrays

Prefer `Ser::Save` / `Ser::Load` on a container field. When a hand-written loop is unavoidable, every element sits in its own `BeginArrayElement()` scope:

```cpp
size_t count = items.size();
if (serializer.BeginArray("items", "item", count)) {
    for (const auto& item : items) {
        if (serializer.BeginArrayElement()) {
            item.Serialize(serializer);
            serializer.EndArrayElement();
        }
    }
    serializer.EndArray();
}
// Reading: BeginArray fills `count`; loop count times over BeginArrayElement() / EndArrayElement().
```

## Binary Format

`BinarySerializer` / `BinaryDeserializer` take the same call sequence as the XML pair, so every type that serializes to XML serializes to binary without changes. Use it for saves and caches where size and speed matter more than readability.

```cpp
BinarySerializer serializer;
if (serializer.BeginObject("Player")) {
    player.Serialize(serializer);
    serializer.EndObject();
}
serializer.SaveToFile("save.bin");          // or serializer.GetData() -> eastl::vector<uint8_t>

BinaryDeserializer deserializer;
if (deserializer.LoadFromFile("save.bin") && deserializer.BeginObject("Player")) {
    player.Deserialize(deserializer);
    deserializer.EndObject();
}
```

- Layout (`BinaryFormat.h`): header `"BEBN"` + `u16 version` + `u32 payloadSize`, then entries `u8 tag | u16 nameLen | name | payload`. Objects and arrays carry a `u32 size` patched in `EndObject` / `EndArray` / `EndArrayElement`. Little-endian only (`static_assert` on the host). Tag values are part of the format: never renumber, only append.
- Entries are matched **by name**, not by position: skip-defaults, null pointers and empty optionals simply write nothing. `Write()` and `WriteAttribute()` produce the same entry (binary has no attribute/child split). A name written twice reads as the last write.
- The array count in the stream is the number of `BeginArrayElement` calls, not the `count` argument of `BeginArray` (factory-backed vectors skip null elements).
- `LoadFromBuffer` / `LoadFromFile` validate the whole stream up front (header, payload size, tags, sizes, nesting depth <= `BinaryFormat::MaxDepth`). A truncated or corrupt stream fails there with an error and exposes nothing; reads after a successful load never go out of bounds.
- Errors follow XML: `Read()` reports `Element not found`, `ReadAttribute()` / `BeginObject()` / `BeginArray()` stay silent for a missing entry. A type mismatch (for example an `int32` entry read as `float`) is always reported: there is no implicit widening.
- Tests: `tests/BECore/ReflectionTest.cpp`, `Binary*` cases (round-trips, wire-format bytes, corrupt-stream handling). Changing the layout breaks `BinaryWireFormat` on purpose: bump `CurrentVersion` and update the expected bytes.

## Factory Generation (FACTORY_BASE)

`BE_CLASS(Name, FACTORY_BASE)` on a base class generates `Generated/EnumXxx.gen.hpp` (type enum named after the base, `FactoryTraits<Base>` with the XML element name) and an `AbstractFactory<Base>` that derived classes register with from their own `.gen.hpp`. Include the derived class's `.gen.hpp` in its `.cpp`, and keep the library linked with `WHOLE_ARCHIVE` so the registration is not dropped.

Current bases: `ILogSink`, `IWidget`, `IAssertHandler`, `ITest`, `IRenderer`, `ITexture`, `IResource`, `IResourceLoader`, `IApplicationFabric`, `IComponent`, `SceneNode`, `Scene`, `IGame` (`grep -rn FACTORY_BASE src`).

`BE_CLASS(Name, FACTORY_BASE, ELEMENT=element)` fixes the XML element name of the array that holds the base's objects; without it the name comes from the common suffix of the base and its first derived class (`ILogSink` + `ConsoleSink` give `sink`), or from the base name when it has no derived class. `REGISTER_SELF` on a concrete base makes it register itself in its factory next to its derived classes (without it a base registers only when it has none, so `type="UIElement"` would stop working once `UIPanel` exists). A class registers in the factory of the nearest `FACTORY_BASE` above it, through any number of classes in between (`UIButton : UILabel : UIElement`). Changing the generator does not regenerate the files whose sources are unchanged: delete the stale `.gen.hpp` files and `build/<preset>/.meta_cache/<Target>`, then re-run `cmake --preset ...`.

A class has a factory field only when the generator finds `IntrusivePtrAtomic<Base>` or `IntrusivePtrNonAtomic<Base>` in the field's type: the `IntrusivePtr<Base>` alias is not matched, so spell the long name in the declaration of a reflected `eastl::vector<IntrusivePtrNonAtomic<Base>>` (`UIElement::children`). A factory base that declares `bool Initialize(IDeserializer&)` (virtual, as `IWidget` does) gets it called on every loaded element after `Deserialize`, inside the element scope, so it can read more of its own element; it returning `false` removes the element from the vector and reports `<TypeName> failed to initialize` (key `type`). Only the factory vector does this: a single `IntrusivePtr` field does not call it, because a lone pointer cannot be dropped (`Ser::Detail::LoadElement`, test `FactoryInitializeTest`). A loaded element whose `type` is not registered is reported (`Unknown factory type`) and falls back to the base class when that is not abstract. `MakeFromConfig` writes the reported errors to the log (`LogDeserializeErrors`, `Reflection/OpenDeserializer.h`); code that deserializes by hand must check `HasErrors()` itself, or the entry is dropped without a message.

A base class that has no derived class in the scanned directories registers itself in the factory, so it must be instantiable. The parser marks a class as abstract when its body declares a pure virtual method (`= 0;`) and the generator then skips that self-registration. This is what lets `IGame` live in the engine without any game: the engine's own run sees no `IGame` descendant, the game's run sees `Match3Game` and registers it in `AbstractFactory<IGame>`.

## Generated Methods

| Method | Description |
|--------|-------------|
| `GetStaticTypeName()` / `GetStaticTypeMeta()` / `GetStaticFieldCount()` | Static type name, `ClassMeta`, field count |
| `GetTypeMeta()` | Virtual, correct through a base pointer |
| `Is<T>()` / `Cast<T>()` | Runtime type checks (use instead of `dynamic_cast`) |
| `ForEachFieldStatic(obj, func)` | Iterate reflected fields (mutable and const) |
| `Serialize(ISerializer&) const` / `Deserialize(IDeserializer&)` | Write / read |
| `AcceptPropertyVisitor(IPropertyVisitor&)` | In-place editing |

## Extension Points

- `Ser::DataAccessor<T>` specializations are the canonical way to support a new container or pointer shape. Extend save and load symmetrically.
- Keep factory-backed polymorphic containers aligned with the generated enum/factory traits.
- Prefer reflection-driven paths over one-off manual XML loops in component code.

## Best Practices

- `BE_REFLECT_FIELD` on the **same line** as the field.
- Include `.gen.hpp` in a `.cpp` only, never in a header.
- Check `HasErrors()` after deserialization.
- Keep code on the split interfaces: `Serialize(ISerializer&) const`, `Deserialize(IDeserializer&)`.

## Don't

- Reflect raw pointers or references.
- Put `BE_REFLECT_FIELD` on a separate line.
- Include `.gen.hpp` in headers.
- Modify generated files.
- Add an archive-style API that reads and writes through one interface.
