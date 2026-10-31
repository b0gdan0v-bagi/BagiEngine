---
paths:
  - "**/Reflection/**/*"
  - "**/*Test*.h"
  - "**/*Test*.cpp"
---

# Reflection System Guidelines

## Overview

Compile-time reflection with Python code generator (meta_generator). Auto-generates `Serialize()` (write) and `Deserialize()` (read) methods.

## Codegen (meta_generator)

- **Script:** `CI/meta_generator/meta_generator.py`
- **Target config:** `CI/meta_generator/reflection_targets.json` — per target: `source_dirs`, `scan_dirs`, `include_dirs`
- **Run:** `--target BECoreModule --project-root . --output-dir build/Generated --cache-dir build` (optionally `--settings meta_generator_settings.json`)
- **Cache:** `metadata_cache.json` in `cache-dir`

## Architecture

The serialization system uses **separate interfaces** for reading and writing:

- **`ISerializer`** - Interface for writing data (save/export)
  - Methods: `Write()`, `WriteAttribute()`
  - Implementations: `XmlSerializer`, `BinarySerializer`
  
- **`IDeserializer`** - Interface for reading data (load/import)
  - Methods: `Read()`, `ReadAttribute()`
  - Error tracking: `HasErrors()`, `GetErrors()`, `ClearErrors()`
  - Implementations: `XmlDeserializer`, `BinaryDeserializer`

## Macros

| Macro | Purpose |
|-------|---------|
| `BE_CLASS(Name)` | Reflection + auto `Serialize()` + `Deserialize()` |
| `BE_CLASS(Name, FACTORY_BASE)` | Factory/enum generation for hierarchy |
| `BE_REFLECT_FIELD` | Mark field for serialization (same line!) |
| `BE_FUNCTION` | Mark method for reflection |

## Serialization

| Type | XML Format | Method |
|------|------------|--------|
| Primitives (int, bool, string, PoolString) | Attributes | `WriteAttribute()` / `ReadAttribute()` |
| Classes (BE_CLASS) | Child elements | `BeginObject()` + `Serialize()` / `Deserialize()` |

**Field naming:** Underscore prefix is automatically stripped for XML names:
- C++ field: `_clearColor` → XML name: `clearColor`
- C++ field: `_filename` → XML name: `filename`

```xml
<Player health="100" name="Hero">
    <Position x="10" y="20"/>
</Player>
```

## Usage

### Writing (Save/Export)

```cpp
// Class definition
struct Player {
    BE_CLASS(Player)
    BE_REFLECT_FIELD int32_t health = 100;  // ✓ Same line
    BE_REFLECT_FIELD PoolString name;
    BE_REFLECT_FIELD Math::Color _clearColor;  // XML: clearColor
    bool _internal = false;  // Not serialized (no BE_REFLECT_FIELD)
};

// Serialization (writing)
XmlSerializer serializer;
if (serializer.BeginObject("Player")) {
    player.Serialize(serializer);  // const method
    serializer.EndObject();
}
serializer.SaveToFile("save.xml");
```

### Reading (Load/Import)

```cpp
// Deserialization (reading)
XmlDeserializer deserializer;
deserializer.LoadFromFile("save.xml");
if (deserializer.BeginObject("Player")) {
    player.Deserialize(deserializer);  // non-const method
    deserializer.EndObject();
}

// Check for errors
if (deserializer.HasErrors()) {
    for (const auto& err : deserializer.GetErrors()) {
        LOG_ERROR("Deserialize error: {} - {} (line {})", 
                  err.path, err.errorMessage, err.line);
    }
}
```

### Arrays

Arrays require an `elementName` parameter:

```cpp
// Writing
size_t count = items.size();
if (serializer.BeginArray("items", "item", count)) {
    for (const auto& item : items) {
        item.Serialize(serializer);
    }
    serializer.EndArray();
}

// Reading
size_t count = 0;
if (deserializer.BeginArray("items", "item", count)) {
    items.resize(count);
    for (auto& item : items) {
        item.Deserialize(deserializer);
    }
    deserializer.EndArray();
}
```

XML result:
```xml
<items count="2">
    <item>A</item>
    <item>B</item>
</items>
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

Use `BE_CLASS(Name, FACTORY_BASE)` on base class. Include `<Generated/EnumXxx.gen.hpp>`.

| Base Class | Enum | Factory |
|------------|------|---------|
| `ILogSink` | `LogSinkType` | `LogSinkFactory` |
| `IWidget` | `WidgetType` | `WidgetFactory` |

## Generated Methods

| Method | Description |
|--------|-------------|
| `GetStaticTypeName()` | Type name as `string_view` |
| `GetStaticTypeMeta()` | `ClassMeta&` for type |
| `Is<T>()` / `Cast<T>()` | Runtime type checking |
| `Serialize(Serializer&) const` | Auto-generated write method |
| `Deserialize(Deserializer&)` | Auto-generated read method |

## Container and pointer serialization

In `BECore/Reflection/DataAccessor.h`, `Ser::Save` / `Ser::Load` dispatch to `Ser::DataAccessor<T>` specializations for:

- Types with `Serialize`/`Deserialize` — one child element per object
- `eastl::vector<T>` — child elements with `value` attribute or `Serialize()` per element
- `eastl::vector<IntrusivePtr<Base>>` with factory — `type` and `enabled` attributes, creation via `FactoryTraits`
- `IntrusivePtrAtomic<T>` / `IntrusivePtrNonAtomic<T>` — one child element; null = skip

Prefer these overloads in custom `Serialize`/`Deserialize` instead of manual `BeginArray`/`BeginObject` where they fit.

### Current direction (important)

- Treat `Ser::DataAccessor<T>` specializations as the canonical extension point for container/pointer support.
- Keep factory-backed polymorphic container flow aligned with generated enum/factory traits from `meta_generator`.
- When adding new serializable container shapes, extend both serialization and deserialization paths symmetrically.
- Prefer generated/reflection-driven paths over one-off manual XML loops in component code.

## Best Practices

- Put `BE_REFLECT_FIELD` on **same line** as field
- Include `.gen.hpp` in .cpp only
- Use `Is<T>()`/`Cast<T>()` instead of `dynamic_cast`
- Use `WriteAttribute()`/`ReadAttribute()` for primitives
- Check `HasErrors()` after deserialization
- Field names with underscore prefix are automatically stripped for XML
- Keep examples and code aligned with split interfaces: `Serialize(ISerializer&) const`, `Deserialize(IDeserializer&)`

## Don't

- Reflect pointers/references directly
- Put `BE_REFLECT_FIELD` on separate line
- Include `.gen.hpp` in headers
- Modify generated files
- Use old `IArchive` interface (deprecated, use `ISerializer`/`IDeserializer`)
