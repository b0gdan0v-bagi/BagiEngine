# Save Module Guidelines

## Overview

`SaveGameService` (`Save/SaveGame.{h,cpp}`) keeps save games as **slots**: one file `<slot>.save` per slot in a folder that the service mounts in the `FileSystem` as `saves`. A plain class, **no `CoreManager` getter yet**: the caller passes the folder to `Initialize(directory)` and the file system the folder is mounted in to the constructor. Not thread safe (main thread, like `FileSystem`).

Two layers in one class:

1. **Slot files**: `WriteSlot` / `ReadSlot` / `Exists` / `Delete` / `List` move bytes in and out of a slot.
2. **The envelope**: `Save` / `Load` / `Peek` write and read a reflected state inside a file that also says which game wrote it and in which version of its schema.

## Files

| File | Holds |
|------|-------|
| `SaveGame.h` | `SaveGameService`, `SaveSchema {game, version, minVersion}`, `SaveInfo {game, version}`, `SaveErrorKind` (`CORE_ENUM`), `SaveError {kind, detail}`, the constants `kSaveMountPoint`, `kSaveExtension`, `kSaveTempExtension`, `kMaxSlotNameLength`; the thin templates `Save(slot, schema, const State&)` and `Load(slot, schema, State&)` |
| `SaveGame.cpp` | the implementation (the non-template `Save` / `Load` / `Peek` take `eastl::function` callbacks, so the binary serializer stays out of the header) |

Tests: `tests/BECore/Save/SaveGameFilesTest.cpp` (slot files; a local `FileSystem` on a `TestTempDirectory`, no global services) and `tests/BECore/Save/SaveGameTest.{h,cpp}` (the envelope; `SaveGameTest.h` holds the reflected test states, its `.gen.hpp` is included in the `.cpp` only).

## Slot files

- **Folder and mount.** `Initialize(directory)` creates the folder (with its parents) and mounts it as `saves`; a second call unmounts the first folder and mounts the new one, so reads come from the new folder. **The service owns the mount point `saves`**: `Initialize` drops whatever else was mounted under that name, so nothing else should mount it. It returns false when the path is a file or the folder cannot be made, and then changes nothing. Before a successful `Initialize` every call answers `SaveErrorKind::NotInitialized` (the calls that return a plain value: false or an empty list).
- **Reads go through the virtual path** `saves/<slot>.save` (`FileSystem::ResolvePath`); a file that `ResolvePath` finds elsewhere (it falls back to the content roots and the working directory) is not a save: the resolved file must be in the save folder. **Writes land in the mounted folder** (`FileSystem` has no write API).
- **Slot names** are 1 to 64 characters of `[A-Za-z0-9_-]` (`IsValidSlotName`): no dot, slash or space, so a name cannot leave the folder. Every call refuses a bad name with `InvalidSlotName` (`Exists` and `Delete` answer false). Names are case-insensitive where the file system is (Windows, a default macOS volume): `Save` and `save` are one slot there, and `List()` returns the case that is on disk.
- **Writes are atomic.** `WriteSlot` writes `<slot>.save.tmp` and renames it over `<slot>.save` (`std::filesystem::rename`, which replaces an existing file). On any failure the old save stays and the temporary file is removed. `List()` ignores a leftover `.tmp` file, files in subfolders and names that are not slot names; it is sorted by name.
- **Errors** are `std::expected<..., SaveError>`; `detail` is a sentence for the log, never empty. `NotFound` is a slot without a file, `ReadFailed` a file that exists and cannot be read (a folder in its place, for one), `WriteFailed` a refused write. `Corrupt`, `WrongGame`, `VersionTooNew` and `VersionTooOld` are for the contents of a file and come from the envelope layer.

## The envelope

A file is a stream of `BinarySerializer` (`Reflection/BinaryFormat.h`) with one root object:

```
save
├── game     string   the id of the game ("Match3")
├── version  u32      the version of the game's state schema when it was written
└── state    object   filled by the game's reflected Serialize()
```

`SaveSchema` is what the game says about itself: `game`, `version` (written now) and `minVersion` (the oldest it still reads). `SaveInfo` is what a file says: `game`, `version`.

**Two version layers.** `BinaryFormat::CurrentVersion` is the container layout (header, tags, sizes): it belongs to the engine and changes only with the byte layout. `version` in the envelope is the game's schema and belongs to the game. They are independent; the second does not touch the first.

**`Load` checks in this order**, and calls the reader only when all pass: the stream is valid (`LoadFromBuffer` validates it whole; else `Corrupt` with the deserializer's first error as `detail`); there is an object `save` with a readable `game` and `version` (else `Corrupt`); `game` equals `schema.game` (else `WrongGame`); `version <= schema.version` (else `VersionTooNew`); `version >= schema.minVersion` (else `VersionTooOld`); the object `state` exists (else `Corrupt`). After the reader, any error the deserializer recorded (a missing entry read with `Read`, a field of another type) is `Corrupt`. `Save` and `Load` refuse a bad slot name or an uninitialized service before they call the callbacks. `Peek` reads only `game` and `version` (no schema, no `state`): for a list of saves in a load menu.

**What a failed `Load` leaves.** A failure before the reader (every kind but `Corrupt` from the state itself) leaves the target untouched. A `Corrupt` from the reader may leave the state partly filled, so a game with something to protect loads into a temporary object and applies it after a success (the Match3 consumer does).

**Version policy.** Added fields need no version: storage is name-keyed, so a field missing from an old file keeps the default of the new type. Raise `version` when the meaning of existing data changes or a field is removed, and raise `minVersion` when the old shape can no longer be read. An older but accepted file loads and `Load` returns its version in `SaveInfo`, so the game can fix the data up after the load (`if (info->version < 2) { state.lives = 3; }`). A newer file is refused rather than guessed at.

## How a game uses it

```cpp
// The state: a reflected class. Only BE_REFLECT_FIELD members are saved.
struct MyState {
    BE_CLASS(MyState)
    BE_REFLECT_FIELD int32_t score = 0;
    BE_REFLECT_FIELD eastl::string board;
};

constexpr SaveSchema kSchema{"MyGame", 2, 1};  // writes version 2, reads 1 and 2

SaveGameService service(CoreManager::GetFileSystem());  // a CoreManager getter comes with a later slice
service.Initialize(saveFolder);

if (auto saved = service.Save("slot1", kSchema, state); !saved) {
    LOG_ERROR(saved.error().detail.c_str());
}

MyState loaded;  // a temporary: the live state is replaced only after a success
if (auto info = service.Load("slot1", kSchema, loaded)) {
    if (info->version < 2) { /* fix up the older data */ }
    state = std::move(loaded);
} else if (info.error().kind == SaveErrorKind::NotFound) {
    // no save yet
} else {
    LOG_ERROR(info.error().detail.c_str());
}
```

A state that is not a reflected class passes lambdas instead: `service.Save(slot, schema, [&](ISerializer& s) { ... })` and `service.Load(slot, schema, [&](IDeserializer& d) { ... })`; the lambdas see the object `state` as the current scope. A reflected vector of structs by value saves and loads, but the property visitor (the inspector) has no overload for it, so a state with one needs a `PropVisit::Visit` overload like `SaveGameTest.h`'s.
