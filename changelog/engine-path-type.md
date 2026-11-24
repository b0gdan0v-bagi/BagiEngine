# Paths: the engine's own UTF-8 Path type; std::filesystem leaves the engine and its PCH

- Version: 0.17.0
- Entry: engine-path-type
- Date: 2026-11-24
- Bump: minor

## Added
- `Path` (`BECore/File/Path.h`): a UTF-8 path over `String`, made from a `String`, a `StringView` or a literal and read as a `StringView`, with `Parent`, `Filename`, `Stem`, `Extension`, `/`, `IsUnder`, `RelativeTo` and comparison and hash by components, following `std::filesystem::path` component by component; the free `Path*` helpers work on a `StringView` without a copy. Adapt: nothing.
- The file layer (`BECore/File/File.h`) gets what `std::filesystem` did for the engine: `WalkDirectory`, `RemoveTree`, `CopyFileTo`, `CurrentDirectory`, `AbsolutePath`, `CanonicalPath`, `WeaklyCanonicalPath`, `IsSameFile`. Adapt: nothing.

## Changed
- The types check (`CI/scripts/check_types.py`, ctest `BagiTypesCheck`) has a rule `path` for `std::filesystem::path`, with `Path` as its advice. Adapt: a game that runs the check moves its hits to `Path` or marks a real boundary with `//-NoStd: <why>`.
- A file watch whose root ends with a separator reports its changes again (`Path::IsUnder`); `FileSystem` logs and skips a content layer it cannot list instead of failing the scan; the editor's `LayoutStore::Delete` refuses a folder. Adapt: nothing.

## Breaking
- Every engine call that took or returned a `std::filesystem::path` takes or returns a `Path`: `FileSystem` (`Mount`, `AddContentRoot`, `ResolvePath`, `GetRootPath`, `SetRootPath`, `GetMountedPath`, `GetMountLayers`), `ConfigManager::GetConfigFilePath`, `LoadFromFile` / `SaveToFile` of the XML and binary readers and writers and `XmlDocument`, the cook (`CookContent`, `VerifyCooked`, `ListContentFiles`), the save service and the file watch backends. `Path` has no conversion to or from `std::filesystem::path`. Adapt: pass a `Path` (or a UTF-8 `String` / `StringView`, which convert to it), read a result through `View()`, and use the calls of `File.h` instead of `std::filesystem` operations; where a game must keep `std::filesystem` (an OS API the layer lacks), it includes `<filesystem>` itself, marks the line `//-NoStd: <why>` and converts through `View()` and `std::u8string_view`.
- `<filesystem>` is no longer in `BECore/pch.h`, and `BECore/File/FilePath.h` (`PathToUtf8`, added in 0.14.0) is removed. Adapt: include `<filesystem>` in a file that still uses it; `PathToUtf8(p)` of an engine path becomes the `Path` itself.
