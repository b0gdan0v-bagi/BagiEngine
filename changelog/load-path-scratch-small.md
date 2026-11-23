# Load and save paths keep their call-lifetime temporaries in scratch arenas

- Version: 0.15.0
- Entry: load-path-scratch-small
- Date: 2026-11-23
- Bump: minor

## Added
- `ReadFileBytesScratch(path, arena, maxScratchBytes)` (`BECore/File/File.h`): reads a whole file into a `LinearArena` when it fits and onto the heap otherwise; `ScratchFileBytes::Bytes()` is valid while the arena mark lives. `ArenaHashMap<Key, T>` (`Types/Containers.h`), and `StringView` keys hash and compare like `String` keys. Adapt: nothing.

## Changed
- Audio clips and the Ogg decoder, the save slot read, `BinaryDeserializer` and `BinarySerializer`, `FileSystem` listings, the editor's XML layout merge and save, `UIDropdown`, the RmlUi font engine, the room list and `JsonDeserializer` keep their temporaries in scratch or an arena of their own: an Ogg clip load allocates once instead of 386 times, a binary save 9 times instead of 330, a listing of 300 files 937 times instead of 2748. `DecodeOgg` takes an optional decoder scratch size. Adapt: nothing.
- A symlinked file inside a mount is listed under the link's name. Adapt: nothing.

## Breaking
- `BinarySerializer` is no longer movable (it owns the arena its scopes and text table point into). Adapt: construct it where it is used, or hold it through `UniquePtr<BinarySerializer>`.
