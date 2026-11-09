# Save Module Guidelines

## Overview

`SaveGameService` (`Save/SaveGame.{h,cpp}`) keeps save games as **slots**: one file `<slot>.save` per slot in a folder that the service mounts in the `FileSystem` as `saves`. A plain class, **no `CoreManager` getter yet**: the caller passes the folder to `Initialize(directory)` and the file system the folder is mounted in to the constructor. Not thread safe (main thread, like `FileSystem`).

This stage holds only the bytes of a slot. What is inside (a game id, a version, the reflected state) is the next layer in the same header.

## Files

| File | Holds |
|------|-------|
| `SaveGame.h` | `SaveGameService`, `SaveErrorKind` (`CORE_ENUM`), `SaveError {kind, detail}`, the constants `kSaveMountPoint`, `kSaveExtension`, `kSaveTempExtension`, `kMaxSlotNameLength` |
| `SaveGame.cpp` | the implementation |

Tests: `tests/BECore/Save/SaveGameFilesTest.cpp` (a local `FileSystem` on a `TestTempDirectory`, no global services).

## Rules

- **Folder and mount.** `Initialize(directory)` creates the folder (with its parents) and mounts it as `saves`; a second call unmounts the first folder and mounts the new one, so reads come from the new folder. It returns false when the path is a file or the folder cannot be made, and then changes nothing. Before a successful `Initialize` every call answers `SaveErrorKind::NotInitialized` (the calls that return a plain value: false or an empty list).
- **Reads go through the virtual path** `saves/<slot>.save` (`FileSystem::ResolvePath`); a file that `ResolvePath` finds elsewhere (it falls back to the content roots and the working directory) is not a save: the resolved file must be in the save folder. **Writes land in the mounted folder** (`FileSystem` has no write API).
- **Slot names** are 1 to 64 characters of `[A-Za-z0-9_-]` (`IsValidSlotName`): no dot, slash or space, so a name cannot leave the folder. Every call refuses a bad name with `InvalidSlotName` (`Exists` and `Delete` answer false).
- **Writes are atomic.** `WriteSlot` writes `<slot>.save.tmp` and renames it over `<slot>.save` (`std::filesystem::rename`, which replaces an existing file). On any failure the old save stays and the temporary file is removed. `List()` ignores a leftover `.tmp` file, files in subfolders and names that are not slot names; it is sorted by name.
- **Errors** are `std::expected<..., SaveError>`; `detail` is a sentence for the log, never empty. `NotFound` is a slot without a file, `ReadFailed` a file that exists and cannot be read (a folder in its place, for one), `WriteFailed` a refused write. `Corrupt`, `WrongGame`, `VersionTooNew` and `VersionTooOld` are for the contents of a file and come with the layer that reads the envelope.
