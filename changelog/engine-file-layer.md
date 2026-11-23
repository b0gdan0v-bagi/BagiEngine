# File layer: engine-owned files over SDL3, no streams in the engine or its PCH

- Version: 0.14.0
- Entry: engine-file-layer
- Date: 2026-11-23
- Bump: minor

## Added
- The file layer `BECore/File/File.h` over SDL3: `File` with `FileMode` (`Read`, `Write`, `Append`, `CreateNew`), `ReadFileBytes`, `ReadTextFile` (bounded by `kDefaultMaxFileBytes`), `WriteFileAtomically` (`<path>.tmp`, then a rename; a failed write keeps the old file), `GetFileInfo`, `CreateDirectories`, `RemoveFile`, `RenameFile`. Paths are UTF-8 `StringView`; errors are `std::expected<T, String>`. Adapt: nothing; use it instead of `std::ifstream` / `std::ofstream`.
- `TextFileWriter<N>` (`File/TextFileWriter.h`, formatted text into an inline buffer, flushed to a `File`), `TextReader` (`File/TextReader.h`, lines, tokens and numbers over a `StringView`), `PathToUtf8` (`File/FilePath.h`, a `std::filesystem::path` as UTF-8, right on Windows) and `StringXmlWriter` (`Config/StringXmlWriter.h`, a pugixml document into a `String`). Adapt: nothing.

## Changed
- The engine reads and writes files through the layer: save slots and `BinarySerializer::SaveToFile` write atomically; `XmlDocument::LoadFromFile` reads through the layer, so a non-ASCII path opens on Windows; the log sinks format each line into a fixed buffer instead of a `std::string`. Adapt: nothing.
- The log clock reads the UTC offset once at start-up: a run that crosses a daylight-saving switch logs the old offset until the next start. Adapt: nothing.

## Breaking
- `<fstream>` and `<sstream>` are no longer in `BECore/pch.h`. Adapt: a game file that used `std::ifstream`, `std::ofstream` or `std::stringstream` through the PCH moves to the file layer (`ReadTextFile`, `WriteFileAtomically`, `TextFileWriter`, `TextReader`), or includes `<fstream>` / `<sstream>` itself with a `//-NoStd: <why>` mark.
- `Widgets/EditorLayout/UserFileIo.h` is removed. Adapt: `ReadTextFile(path)` becomes `ReadTextFile(PathToUtf8(path))`, and `WriteTextFileAtomically(path, text)` becomes `WriteFileAtomically(PathToUtf8(path), text)`, from `BECore/File/File.h` and `BECore/File/FilePath.h`.
