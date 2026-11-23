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
- **Game targets:** `be_add_game_reflection(<Target>)` passes the game's `src/` as `--source-dir`/`--include-dir` and `--reference-target BECoreModule`. Reference headers (the engine's) are parsed for factory bases, parents and `CORE_ENUM`s, but nothing is generated for them. A game has its own cache (`build/<preset>/.meta_cache/<Target>/`) and no entry in `reflection_targets.json`; its `#include` paths are computed from the game's `src/`, so `src/<Name>/<Name>Game.h` of a game is included as `<<Name>/<Name>Game.h>`.
- **Manifest:** every run writes `--manifest` (`.meta_cache/<Target>/generated_files.txt`): the `.gen.hpp` files that run owns, including the ones that were up to date. CMake adds exactly those to the target; the engine and the games share `Generated/`, so it is never globbed.
- **Manual run:** `--target BECoreModule --project-root . --output-dir build/Generated --cache-dir build` (engine), or `--source-dir tools/stress/src --include-dir tools/stress/src --reference-target BECoreModule --project-root . --output-dir ... --cache-dir ...` (game). The cache is `metadata_cache.json` in `cache-dir`.
- Output goes to `build/<preset>/Generated/`. Never edit generated files.
- **Template and the cache:** `templates/reflection.gen.hpp.j2` also writes `ReflectionTraits<T>::Parent` (a `BE_CLASS` parent that has `BE_CLASS`, the first base only), the parameter names of every method and, per class and event, a static initializer that registers its `TypeInfo` in `TypeRegistry`. It also defines `WriteJson` next to `Serialize` (keys sorted, one `JsonWrite::SaveField` per field) and, for a `BE_CLASS`, `WriteJsonMembers`, which a class with a reflected parent merges with its parent's by key (`JsonWrite::MemberMerge`). A change of the template does not regenerate a source that did not change, so it **bumps `MetadataCache.CACHE_VERSION`** (`core/cache.py`); a game picks it up at its next `cmake --preset`. The parameters of the generated functions that use a field unqualified (`s` of `Serialize`, `d` of `Deserialize`, `w` of an event's `WriteJson`) are visible to a field of the same name: a field named so does not compile in them (`AcceptPropertyVisitor` names its parameter `propertyVisitor` for this reason).
- **Warnings:** the parser is regex based, so a marker it cannot turn into metadata is reported and never dropped silently. Every run prints one line per skipped marker to stderr: `path:line: warning: reflection: <what and why>` (the line is the marker's line in the file, as are the `line` values of classes, fields and methods in the cache; the compiler and the editors can follow it). The warnings are stored with the file in the cache (`FileMetadata.warnings`, `CACHE_VERSION`), so an unchanged file prints them again on every configure, not only the first time. Only the files of the target itself are reported (`source_dirs` and `scan_dirs`, not the reference dirs of another target: the engine's run owns the engine's warnings). The summary line says `N reflection warnings`; `--quiet` prints nothing. A warning never changes the exit code, because CMake stops the configure on a non-zero one; `--warnings-as-errors` (not passed by CMake) exits with 1 when there is any warning, also with `--quiet`. A `FACTORY_BASE` whose generated enum name (`ILogSink` -> `LogSinkType`) is already declared by a handwritten `CORE_ENUM` in the scanned sources gets a warning too. A header whose `Generated/<Stem>.gen.hpp` is included by no `.cpp` of the target (its `source_dirs`, `scan_dirs` and include dirs) warns on the line of its first `BE_CLASS` / `BE_EVENT`, because `Serialize` / `Deserialize` are then never defined (a link error); this one is computed from the `.cpp` files on every run, not stored in the cache, so editing a `.cpp` changes the answer for an untouched header. The real tree (engine, BagiStress, Chess) prints none: a new warning is a marker that does nothing, so rewrite the declaration or extend the parser.
- **Tests of the generator:** pytest, not part of ctest: `.venv/bin/python -m pytest CI/meta_generator/tests` (from the engine root; `pytest` is in `requirements.txt`). The files pin the parser construct by construct (`test_parser_classes.py`, `test_parser_fields.py`, `test_parser_methods.py`), the purpose rule (`test_purpose_check.py`), the warnings (`test_warnings.py`) and the cache and command line (`test_cache_and_cli.py`; the `main()` cases skip without `jinja2`). A construct that is a limitation has a test that asserts its warning, so a parser change that supports it shows up as a failing test to update.
- **What the regex parser does not handle** (each one warns, except where marked "silent"):
  - Class forms: `class Foo final : ...`, an export macro (`class MY_API Foo`), `alignas(...)` and an attribute before the name (`class [[nodiscard]] Foo`), a template specialization (`struct Foo<int>`) are not recognised, so the `BE_CLASS` and every marker inside warn. A class template header (`template<typename T> struct Foo`) is fine. The first `BE_CLASS` / `BE_EVENT` in a class must name that class; a reflected class nested in a reflected class warns for the nested markers (reflect nested classes separately).
  - Fields: one declarator per `BE_REFLECT_FIELD` (`int a, b;` and `int a = 1, b = 2;` warn), no arrays (`float _m[16]`), bitfields or function pointers, and the declaration ends at its first `;` outside brackets. `static` / `constexpr` stay in the type (**silent**, such a field cannot be serialized), `mutable` and `[[attributes]]` are dropped. `BE_FIELD_ATTRS(...)` warns, and the field is still reflected without the attribute, for an unknown name, a wrong argument count or kind (`range` needs two numbers with min <= max, `step` one number > 0, `tooltip` one string literal, the flags none), a duplicate, `range` / `step` on a bool, a string or an enum, and a list with no `BE_REFLECT_FIELD` right before it (ignored). A list with no parenthesis or no closing one drops its field too.
  - Methods: `BE_FUNCTION` needs a return type (a constructor warns), is read by its name (`&Class::Name`), so templates, operators other than `operator()` and overloads that make `&Class::Name` ambiguous cannot be reflected (templates and operators warn, an overload is **silent** and fails to compile in the `.gen.hpp`). An inline body (`BE_FUNCTION virtual void DrawScreen() {}`) ends the declaration. A declaration without `;` that runs into the next marker warns once.
  - No preprocessor and no scoping: a class in an `#if 0` block or behind an undefined macro is generated like any other, a class declared in a function body is picked up (**silent**; the generated code cannot name it), `inline namespace v1` is a segment of the name (`A::v1::Foo`), and an unknown option of `BE_CLASS` (`FACTORY_BAS`) is ignored (**silent**).
  - Comments and the contents of string, character and raw string literals are blanked before parsing (newlines kept), so a marker in them is no marker and `"http://x"` does not eat the line.

## Architecture

Separate interfaces for writing and reading, sharing the structural base `IArchiveBase` (`BeginObject` / `EndObject`, `BeginArray` / `EndArray`, `BeginArrayElement` / `EndArrayElement`):

- **`ISerializer`**: `Write()`, `WriteAttribute()`. Implementations: `XmlSerializer`, `BinarySerializer`.
- **`IDeserializer`**: `Read()`, `ReadAttribute()`, errors via `HasErrors()` / `GetErrors()` / `ClearErrors()` (`DeserializeError`: `path`, `errorMessage`, `line`); findings of a load via `SetDiagnostics(DiagnosticsMode)` (`Off`, `Warn`, `Error`) / `GetWarnings()`, `HasKey(name)`, `NoteKnownKey(name)` and `FinishLoad()` (see "Load diagnostics"). Implementations: `XmlDeserializer`, `BinaryDeserializer`.
  Ask the engine for one instead of constructing `XmlDeserializer` yourself: `CoreManager::GetConfigManager().CreateDeserializer(name)` for a named config, `OpenDeserializer(virtualPath)` (`Reflection/OpenDeserializer.h`) for a file, whose format follows the extension (`.xml`, `.bin`).
- **`IPropertyVisitor`**: in-place field editing (ImGui inspector, `ImGuiPropertyVisitor`), reached through the generated `AcceptPropertyVisitor()`. Every field is visited between `BeginField(FieldAttributes)` and `EndField()` (nested around a compound's children; `PropVisit::VisitField`); a `hidden` field is skipped unless the visitor's `ShowsHidden()` is true. A container has hooks of its own: `BeginList(name, ListInfo)` / `EndListElement(i, ListRequest&)` / `EndList(ListRequest&)` for a sequence or a map, `VisitNewKey` for the key of a map's new entry, `VisitPresence` for an `Optional`, `VisitAlternative` for a `Variant`, `VisitValueField` for a `Value<T>` (its kind, source and an `explain` of it: `Values/Explain/CLAUDE.md`); a request (add, remove) is advisory and applied by the dispatch once, after `EndList`, never inside the loop, and only when the `ListInfo` allowed it. Every hook has a default, so a visitor that overrides none edits every field and every element, skips the `hidden` fields, and adds, removes, engages or switches nothing.
- **`TypeRegistry`** (with `TypeDesc` and `TypeInfo`): every `BE_CLASS` and `BE_EVENT` registers itself at start-up, so a program finds a type, its fields, methods and parents by name and makes an object of a factory type by name; the access layer (`TypedRef`, `GetProperty` / `SetProperty`, compiled paths, `CallMethod`) reads, writes and calls by name, and the control API puts it in front of a client (`get_property`, `set_property`, `call_method`, `list_types`, `describe_type`, `schema` over the roots; `SchemaExport` writes the registry as JSON). `tools/schema` turns that JSON into an XSD per config file for the editor. Contracts and reasons: `src/Modules/BECore/Reflection/CLAUDE.md`.

## Macros

| Macro | Purpose |
|-------|---------|
| `BE_CLASS(Name)` | Reflection + `Serialize` / `Deserialize` + `AcceptPropertyVisitor` + `Is<T>()` / `Cast<T>()` |
| `BE_CLASS(Name, FACTORY_BASE)` | Also generates the type enum and `AbstractFactory` registration for the hierarchy |
| `BE_CLASS(Name, ICON=<lucide-name>)` | `ClassMeta::icon` (empty by default): a Lucide name in kebab case, `[a-z0-9-]+`, no prefix. The core knows no font; the editor maps the name to a glyph (`IconTable`, `.claude/rules/widgets-module.md`). Combines with the other options; an empty or malformed value warns (`ICON=... ignored`) and is dropped (clang-format writes `ICON = layout - dashboard`; the parser takes the blanks around a hyphen out). Classes only: `BE_EVENT` has none |
| `BE_CLASS(Name, PURPOSE=sim\|visual\|editor)` | `ClassMeta::purpose` and `ReflectionTraits<T>::purpose` (`AssetPurpose`, default `Common`: plain data allowed in every asset part). Declared per class, not inherited; the value is case-insensitive, an unknown or empty one warns (`PURPOSE=... ignored`) and stays `Common`. The generator exits 1 with `path:line: error: reflection: 'A' (purpose sim) has a field of type 'B' (purpose visual)` when a field's type names (any identifier in it) a class of another non-`Common` purpose, or a `Common` class names a purposed one; unknown or ambiguous names are skipped. The load-time half is the deserializer's purpose filter (`Resource/CLAUDE.md`, "Asset parts"). Classes only: `BE_EVENT` has none |
| `BE_EVENT(Name)` | Lightweight variant for events (`.claude/rules/reflection-events.md`) |
| `BE_REFLECT_FIELD` | Mark a field for serialization (same line as the field) |
| `BE_FIELD_ATTRS(...)` | Metadata of the field, written right after `BE_REFLECT_FIELD` on the same declaration: `range(min, max)`, `step(s)`, `tooltip("text")`, and the flags `readonly`, `hidden`, `required`, `no_factory` (generator only, not in `FieldAttributes`: see "Factory Generation"). It is an empty macro on every compiler; only the generator reads it from the source text, into a constexpr `FieldAttributes` per field in `ReflectionTraits<T>::fields` (`FieldInfo::attributes`). A separate macro, not `BE_REFLECT_FIELD(...)`, because `BE_REFLECT_FIELD` has no arguments in about 530 places. The inspector uses them (slider, snap, tooltip, disabled, not drawn: `ImGuiPropertyVisitor`); on a container field `range` and `step` mean its numeric elements (they are visited inside the field's `BeginField`), the rest apply to the whole field. A load checks `range` and `required` when its diagnostics are on (see "Load diagnostics"): the generated `Deserialize` of a field that has attributes calls `Ser::Load(d, field, "key", attributes)`, and a field without any keeps the short call; the `range` check walks a container's numeric elements, `required` is one check of the field |
| `BE_FUNCTION` | Mark a method for reflection: it is listed in the schema and called by name (`CallMethod`) when `callable`; a client reaches it through `call_method` from a control root (`game` is the active `IGame`). A lifecycle override that a loader or manager calls through its base (the `IWidget` methods) is not marked |

## Serialization

The generated `Serialize` calls `Ser::Save(s, field, "name")` for every field, `Deserialize` calls `Ser::Load`. `Ser::Save` / `Ser::Load` (`Reflection/DataAccessor.h`) pick a `Ser::DataAccessor<T>` specialization:

| Type | Written as |
|------|-----------|
| arithmetic, `String` (a string of any allocator), `FixedString`, `PoolString`, reflected enum (`CORE_ENUM`) | attribute (`WriteAttribute` / `ReadAttribute`); a `FixedString` that cannot overflow keeps the part of a longer text that fits and reports the rest |
| type with `Serialize` / `Deserialize` | child object: `BeginObject(key)` + `Serialize` |
| `Vector<T>`, `SmallVector`, `FixedVector`, `Array` (the `eastl::` forms of any allocator too) | array; elements are objects or a `value` attribute (a `FixedVector` keeps the items that fit and reports the rest) |
| `HashMap`, `FixedHashMap`, `Map` | array of `entry` with a `key` attribute, in key order for the hash maps (a `FixedHashMap` that cannot overflow keeps the entries that fit and reports the rest) |
| `Optional<T>` (`std::optional`) | the value; nothing when empty |
| `Variant<Ts...>` (`std::variant`) | child object with an `index` attribute and the active alternative under `value` (an attribute for a scalar, a child for an object); `std::monostate` writes the index alone. As an element of an array or the value of a map the two names sit in the element itself. A missing index (`Variant index is missing`) or an out-of-range one (`Invalid variant index <n>`) is an error and keeps the variant; a valid index emplaces a default alternative before reading it. The alternatives must be default constructible and have an accessor (`static_assert`). `eastl::variant` has none |
| `Vector<IntrusivePtr<Base>>` with a factory | array; each element has `type` (class name) and `enabled` attributes, created through `AbstractFactory`; `Initialize()` is called on loaded elements when present, and so is `bool Initialize(IDeserializer&)` (declared on the base, like `IWidget`'s) inside the element's own scope: `false` drops the element and reports `<TypeName> failed to initialize` |
| `IntrusivePtr<T>` | one child element; null is skipped |

`FlatMap`, `FlatSet`, `HashSet`, `Set`, `Span`, `UniquePtr`, `SharedPtr` and `Tuple` have no accessor, so a field of one does not compile (`Ser::HasDataAccessor<T>` is false; `ReflectionAliasesTest` pins the list, and the line to remove when support is added on purpose). A container as the element of a container (`Vector<Vector<int>>`) does not serialize either.

The property visitor (`PropVisit::Visit`, `PropertyVisitorDispatch.h`) edits arithmetic types, strings, enums, reflected objects, the sequences (`Vector`, `SmallVector`, `FixedVector`, `Array`: never resized; a `FixedVector` adds only while it has room), the maps (values under the text of their key; an add takes a text or integer key), `Pair`, `Optional` and `Variant`, with any of these as elements; `Vector<IntrusivePtr<T>>` edits its objects and has no add or remove (the class of a new element would be a choice no hook offers). An element is visited by an unqualified call inside a template, so a type whose own `PropVisit::Visit` overload is declared after `PropertyVisitorDispatch.h` (`UIColor`) is not found for an element: the element gets the overload the header has for it. A type that must not be edited keeps a non-template stub returning `false` (`SceneView.h`, `UIVariant.h`): a non-template beats the container templates.

**Data a fixed container drops on load** (a `FixedString`, `FixedHashMap` or `FixedVector` that cannot overflow) is an error of the field like any bad input: `HasErrors()` is true and the message gives the sizes (`kept 2 of 3 entries`), never the content; a `SaveGameService` load of such a state is `Corrupt`.

**Strict values:** `IDeserializer::SetStrictValues(true)` (`XmlDeserializer` implements it; `BinaryDeserializer` has no text to check and keeps the default) makes the reads of a bool, the integer types, `float` and `double` check the whole text: a bool must be `true` / `false` / `1` / `0`, a number must be a number of the target type (spaces around it and a leading `+` are fine; `abc`, `12abc`, `0x10`, `1.5` for an integer, `-1` for an unsigned one, `65536` for a `uint16_t`, `nan` are not). A bad text is an error `Invalid boolean|integer|number value '<text>'` (with ` (out of range)` for a number that parses but does not fit) under the path of the field, `ReadAttribute` / `Read` return `false` and the field keeps its value; a missing attribute is still no error. It is off by default, on purpose: `XmlDeserializer` is lenient (a malformed number is 0, any other bool text is false) and callers like `UIDocument` reject a whole document on any error, so a default change would reject existing files. A caller that wants the checks (for example a config with a port, or the command-line and JSON sources) turns it on before `Deserialize`.

**Load diagnostics:** `IDeserializer::SetDiagnostics(DiagnosticsMode)` makes a load say what it finds odd, apart from its errors: `Off` (default) records nothing, `Warn` stores a finding in `GetWarnings()` (same `DeserializeError`, same cap and text cut as the errors; `HasErrors()` stays false), `Error` makes it an ordinary error. A finding is about content that loaded: the field keeps what was read. The findings are a `required` field whose key is missing (`HasKey`), a number outside `range` (the bounds are inside; `NaN` is outside) and, in `XmlDeserializer` only, an unknown key: an attribute or child element of a scope that nothing read there, with `did you mean 'x'?` when a key that was asked for in that scope is near (edit distance at most 2 and under half the name's length). What counts as read is what was asked for, found or not, so a malformed value is an error and never also an unknown key; an array scope checks its own attributes only, its children are the elements. A finding inside a container names the element: `key[i]` (`i` the position in the container's own iteration), `key.value` for a variant's alternative; a variant's `index` and `value` are read keys like any other. A reader that takes a key of the current scope by hand, beside the reflected struct, calls `NoteKnownKey(name)`; the root scope of a document that is deserialized with no `BeginObject` (a config's struct) is closed by `FinishLoad()`, which the caller calls after `Deserialize`. It is off by default, on purpose, for the same reason as strict values: saves and `UIDocument` reject a whole document on any error, so a new finding must not make an existing file fail. The config path turns it on (`ConfigManager::GetDiagnostics()`: `Warn`, `Error` with the command-line flag `--strict-config`; `Config/CLAUDE.md`), and `tests/Common/ConfigFilesCheck.h` checks a folder of configs under `Error` (`ExpectConfigFilesClean`).

**Field naming:** a leading underscore is stripped for the serialized name (`_clearColor` → `clearColor`).

```cpp
struct Player {
    BE_CLASS(Player)
    BE_REFLECT_FIELD int32_t health = 100;      // same line as the marker
    BE_REFLECT_FIELD PoolString name;
    BE_REFLECT_FIELD Math::Color _clearColor;   // serialized as "clearColor"
    BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("Opaque at 1")) float _opacity = 1.0f;  // an inspector slider
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
        LOG_ERROR("Deserialize error: {} - {} (line {})", err.path, err.errorMessage, err.line);
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
// Reading: BeginArray fills `count`; `container.reserve(count)`, then loop count times over BeginArrayElement() / EndArrayElement().
```

**Reader limits** (`Reflection/ReaderLimits.h`, tests `tests/BECore/ReaderLimitsTest.cpp`): both readers refuse input beyond a named limit with an error, never an assertion. A text over 4 MiB or a binary stream over 16 MiB (a file is checked by its size before it is read, and a path that is not a regular file is refused), an array of more than 65536 elements, XML nested more than 256 open entries (a recursive type, such as a UI tree, recurses once per two), and a list of errors that stops at 64 entries and says so in its last one (an error's path and message are cut to 256 bytes, a quoted bad value to 32, and a control character in them becomes `?`: they reach logs and replies), and a value read into a `PoolString` field of more than 4096 bytes (`kMaxPoolStringBytes`: a name, an id or a key; the pool never frees, so XML, binary and JSON refuse a longer one with an error before `Intern` and the field keeps its value; a `String` field has no such limit). The limits are about 250 times what the content has: a game with more raises the constant. `XmlDeserializer::LoadFromString` reads the text as UTF-8 whatever its first bytes are; any load drops the earlier document, so after a refusal nothing is readable.

## Binary Format

`BinarySerializer` / `BinaryDeserializer` take the same call sequence as the XML pair, so every type that serializes to XML serializes to binary without changes. Use it for saves and caches where size and speed matter more than readability. A save game goes through `SaveGameService` (slots, game id and version, atomic write; `src/Modules/BECore/Save/CLAUDE.md`), not by hand with `SaveToFile`.

```cpp
BinarySerializer serializer;
if (serializer.BeginObject("Player")) {
    player.Serialize(serializer);
    serializer.EndObject();
}
serializer.SaveToFile("save.bin");          // or serializer.GetData() -> Vector<uint8_t>

BinaryDeserializer deserializer;
if (deserializer.LoadFromFile("save.bin") && deserializer.BeginObject("Player")) {
    player.Deserialize(deserializer);
    deserializer.EndObject();
}
```

- Layout (`BinaryFormat.h`): header `"BEBN"` + `u16 version` + `u32 payloadSize`. **Version 3** (what the writer writes) is a name table, a pool section, then entries `u8 tag | u32 nameIndex | payload`; the name table is `u32 count | u32 charBytes | u32 end[count] | chars`, **strictly ascending** in `BinaryFormat::CompareStrings` order, so each name is there once; the pool section is `u32 count | u32 bytes | u32 hashFunction | u32 reserved (0) | u64 checksum` and `count` entries `u64 hash | u32 size | chars | NUL | padding to 8` (the layout of a pool entry), sorted by the same order. **Version 2** (one table for the names and the `PoolString` texts) and **version 1** (entries `u8 tag | u16 nameLen | name | payload`, no table) are still read, for saves of players. Objects and arrays carry a `u32 size` patched in `EndObject` / `EndArray` / `EndArrayElement`. Little-endian only (`static_assert` on the host). Tag values are part of the format: never renumber, only append.
- **What is tabled:** every entry name (name table) and the text of a `PoolString` value (tag 15, a `u32` index of the pool section; in version 2 the one table). A `String` value stays inline (tag 12) and is never interned: the pool never frees; so does a `PoolString` value over `ReaderLimits::kMaxPoolStringBytes`, which a reader reports when it is read into a `PoolString`. A reader takes tags 12 and 15 as one kind, so a field that changes between `String` and `PoolString` keeps loading. A section text is interned only when a `PoolString` read needs it, all of the file's in one `PoolString::InternBatch` at the first such read: a `Peek` of a save or a read of other fields grows no pool, and names are never interned. The reader hashes every text itself and ignores `hash`, `hashFunction` and `checksum` (they are written for a loader that may trust them; a save is never such a loader); a `PoolString` read into a `String` takes the text from the section and interns nothing.
- Entries are matched **by name**, not by position: skip-defaults, null pointers and empty optionals simply write nothing. `Write()` and `WriteAttribute()` produce the same entry (binary has no attribute/child split). A name written twice reads as the last write.
- The array count in the stream is the number of `BeginArrayElement` calls, not the `count` argument of `BeginArray` (factory-backed vectors skip null elements).
- `LoadFromBuffer` / `LoadFromView` / `LoadFromFile` validate the whole stream up front (header, payload size, tags, sizes, nesting depth <= `BinaryFormat::MaxDepth`, and whether any scope holds one name twice in one tag class). A truncated or corrupt stream fails there with an error and exposes nothing; reads after a successful load never go out of bounds.
- **Who owns the bytes:** `LoadFromBuffer` copies them, `LoadFromFile` reads straight into the buffer the reader keeps, `LoadFromView` keeps the caller's pointer and copies nothing: the memory must stay alive and unchanged until the next load, a failed load or the reader's destruction (the reader cannot check it; `SaveGameService` holds its bytes beside the reader). A mapped file or a cooked blob uses `LoadFromView`.
- **Lookup:** a scope remembers where the entry after its last match is. Versions 2 and 3 first compare the text of that entry with the name (no search, so a field read in the order it was written costs one step); on a miss they find the name by one binary search in the sorted name table (no table is built at load; in version 3 the pool section is not searched at all) and compares entries by index, so a name that is not in the file costs no scan of any scope. Version 1 has no table: it compares names from the cursor round to the start, and a name that is not in the scope costs one scan of it. "Last write wins" survives because the validation sets a per-file flag when a scope has a duplicate; such a file takes the full forward scan. `HasKey` never moves the position, so a `required` check followed by the read of the same name stays cheap.
- Errors follow XML: `Read()` reports `Element not found`, `ReadAttribute()` / `BeginObject()` / `BeginArray()` stay silent for a missing entry. A type mismatch (for example an `int32` entry read as `float`) is always reported: there is no implicit widening.
- **Content mode (cooked `config/*.bin`):** the same format and the same version, plus a **content stamp**: the Object entry `$content` (`BinaryFormat::ContentStampName`, a name no field can have) first in the root scope, with `root` (the type name, a `String`) and `layout` (`ComputeLayoutHash` of that type, a `UInt64`). `WriteContentStamp` writes it, `CheckContentStamp` refuses a file with no stamp, an unknown type or another hash with a message that says to cook again; `ConfigManager` does it at load, so a file of another layout is never half read. The hash covers names, kinds and the derived types of pointer fields, never offsets or the compiler's spelling of a type (a file cooked on one compiler loads on another). A save has no stamp (its tolerant name matching stays) and a reader that does not look for it ignores it. The file is read through `LoadFromView` over bytes the owner keeps (`Config/CLAUDE.md`, "Binary configs"). End to end: the XML sources are cooked by the game's own executable (`--cook=<dir>`, the layout hash needs its types), each file stamped with its root type, and `--cook-verify` proves the `.bin` reads as its XML (`Cook/CLAUDE.md`).
- Tests: `tests/BECore/ReflectionTest.cpp`, `Binary*` cases (round-trips, wire-format bytes, corrupt-stream handling), `BinaryPoolSectionTest.cpp` (the section's parser and checksum) and `BinaryDeserializerTest.cpp` (each reading case runs over versions 2 and 3). Changing the layout breaks `BinaryWireFormat` on purpose: bump `CurrentVersion` and update the expected bytes; the previous versions stay readable, and their bytes are kept in tests (`tests/Common/BinaryStreams.h` writes any version, with stale hashes or another hash function on request; `SaveGameTest` holds literal saves of versions 1 and 2).

## Factory Generation (FACTORY_BASE)

`BE_CLASS(Name, FACTORY_BASE)` on a base class generates `Generated/EnumXxx.gen.hpp` (type enum named after the base, `FactoryTraits<Base>` with the XML element name) and an `AbstractFactory<Base>` that derived classes register with from their own `.gen.hpp`. Include the derived class's `.gen.hpp` in its `.cpp`, and keep the library linked with `WHOLE_ARCHIVE` so the registration is not dropped.

Current bases: `ILogSink`, `IWidget`, `IAssertHandler`, `ITest`, `IRenderer`, `ITexture`, `IResource`, `IResourceLoader`, `IApplicationFabric`, `IComponent`, `SceneNode`, `Scene`, `IGame` (`grep -rn FACTORY_BASE src`).

`BE_CLASS(Name, FACTORY_BASE, ELEMENT=element)` fixes the XML element name of the array that holds the base's objects; without it the name comes from the common suffix of the base and its first derived class (`ILogSink` + `ConsoleSink` give `sink`), or from the base name when it has no derived class. `REGISTER_SELF` on a concrete base makes it register itself in its factory next to its derived classes (without it a base registers only when it has none, so `type="UIElement"` would stop working once `UIPanel` exists). A class registers in the factory of the nearest `FACTORY_BASE` above it, through any number of classes in between (`UISegmented : UIStack : UIElement`). Changing the generator or its template does not regenerate the files whose sources are unchanged (a parser change that bumps `CACHE_VERSION` does): delete the stale `.gen.hpp` files and `build/<preset>/.meta_cache/<Target>`, then re-run `cmake --preset ...`.

A class has a factory field when the generator finds `IntrusivePtr<Base>` in the field's type (`Vector<IntrusivePtr<Base>>`, `UIElement::children`); a game's old spellings `IntrusivePtrAtomic<Base>` and `IntrusivePtrNonAtomic<Base>` are matched too. `BE_FIELD_ATTRS(no_factory)` takes a field out: the scene tree (`Scene::_nodes`, `SceneManager::_scenes`, `SceneNode::_children` / `_components`) is not built by a factory field, and inlining `FactoryTraits<SceneNode>` / `<IComponent>` into those gen files would define them a second time (an ODR risk: a game target lists other derived classes). A factory base that declares `bool Initialize(IDeserializer&)` (virtual, as `IWidget` does) gets it called on every loaded element after `Deserialize`, inside the element scope, so it can read more of its own element; it returning `false` removes the element from the vector and reports `<TypeName> failed to initialize` (key `type`). Only the factory vector does this: a single `IntrusivePtr` field does not call it, because a lone pointer cannot be dropped (`Ser::Detail::LoadElement`, test `FactoryInitializeTest`). A loaded element whose `type` is not registered is reported (`Unknown factory type`) and falls back to the base class when that is not abstract. `MakeFromConfig` writes the reported errors to the log (`LogDeserializeErrors`, `Reflection/OpenDeserializer.h`); code that deserializes by hand must check `HasErrors()` itself, or the entry is dropped without a message.

A base class that has no derived class in the scanned directories registers itself in the factory, so it must be instantiable. The parser marks a class as abstract when its body declares a pure virtual method (`= 0;`) and the generator then skips that self-registration. This is what lets `IGame` live in the engine without any game: the engine's own run sees no `IGame` descendant, the game's run sees the game's `IGame` descendant and registers it in `AbstractFactory<IGame>`.

## Generated Methods

| Method | Description |
|--------|-------------|
| `GetStaticTypeName()` / `GetStaticTypeMeta()` / `GetStaticFieldCount()` | Static type name, `ClassMeta`, field count |
| `GetTypeMeta()` | Virtual, correct through a base pointer |
| `Is<T>()` / `Cast<T>()` | Runtime type checks (use instead of `dynamic_cast`) |
| `ForEachFieldStatic(obj, func)` | Iterate reflected fields (mutable and const) |
| `Serialize(ISerializer&) const` / `Deserialize(IDeserializer&)` | Write / read |
| `WriteJson(JsonWriter&) const`, `WriteJsonMembers(w, outer) const` | The object `Serialize` saves into a `JsonSerializer`, written straight into a `JsonWriter` as the same bytes (`Control/JsonWriteReflect.h`); `WriteJsonMembers` is its members without the braces, merged by key with the classes of the chain |
| `AcceptPropertyVisitor(IPropertyVisitor&)` | In-place editing |

## Extension Points

- `Ser::DataAccessor<T>` specializations are the canonical way to support a new container or pointer shape. Extend save and load symmetrically. A new container also gets a `PropVisit::Visit` overload declared with the other containers' in `PropertyVisitorDispatch.h` (before any is defined, so it is found as an element). A partial specialization does not match a class derived from the template (`FixedString`, `FixedHashMap`): give such a type its own, and write none that names one allocator.
- Keep factory-backed polymorphic containers aligned with the generated enum/factory traits.
- Prefer reflection-driven paths over one-off manual XML loops in component code.

## Best Practices

- `BE_REFLECT_FIELD` on the **same line** as the field.
- Include `.gen.hpp` in a `.cpp` only, never in a header.
- Check `HasErrors()` after deserialization; a reader of a config calls `FinishLoad()` first and logs with `LogDeserializeErrors`.
- Keep code on the split interfaces: `Serialize(ISerializer&) const`, `Deserialize(IDeserializer&)`.

## Don't

- Reflect raw pointers or references.
- Put `BE_REFLECT_FIELD` on a separate line.
- Include `.gen.hpp` in headers.
- Modify generated files.
- Add an archive-style API that reads and writes through one interface.
