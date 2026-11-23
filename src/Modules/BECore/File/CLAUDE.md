# File Module Guidelines

## Overview

The engine's own file layer over SDL3 (`SDL_IOStream`, `SDL_GetPathInfo`, `SDL_RenamePath`), in `File.h`; `File.cpp` is the only file that includes SDL for it. Why not `std::` streams: no `filebuf`, locale or `std::string`, the layer's own memory goes through the allocation tracker (SDL's allocator is routed to it, `Profiler/Alloc/CLAUDE.md`), and one atomic write replaces the three copies that were written by hand. The whole-file readers, the save and XML writers, the log sink, the editor's user files and the Games recorders go through it (`StringXmlWriter` is in `Config/`, it needs pugixml). The layer is not in `pch.h`: include `<BECore/File/File.h>` (`<BECore/File/Path.h>` for the path type).

| API | Does |
|-----|------|
| `File::Open(path, FileMode)` | one open file, move-only, closed by the destructor; `Read`, `Write`, `Flush`, `Size`, `Close` |
| `GetFileInfo(path)` | `Optional<FileInfo>` (kind, size, mtime); nullopt for a missing or unreadable path; allocates nothing on POSIX |
| `CreateDirectories`, `RemoveFile`, `RenameFile` | folder tree, one file, a rename that replaces a file |
| `WalkDirectory(folder, WalkMode, visit)` | the entries of a folder or of a tree through a `FunctionRef<bool(relativePath, FileKind)>`; false from `visit` stops, as a success |
| `RemoveTree`, `CopyFileTo(from, to, CopyMode)` | a file or a whole folder; a file copied (`Fail` or `Overwrite`) |
| `CurrentDirectory`, `AbsolutePath`, `CanonicalPath`, `WeaklyCanonicalPath`, `IsSameFile` | the working folder; `std::filesystem::absolute(p).lexically_normal()`; `realpath` (the path must exist); canonical of the part that exists plus the rest cleaned up; same device and index |
| `ReadFileBytes`, `ReadTextFile` | a whole file in one allocation, refused above `maxBytes` (default `kDefaultMaxFileBytes`) before anything is allocated |
| `ReadFileBytesScratch(path, arena, maxScratchBytes)` | `ReadFileBytes` into the caller's `LinearArena` (a `ScratchScope`) when the file is at most `maxScratchBytes`, else into a heap block the `ScratchFileBytes` owns; `Bytes()` is the span either way |
| `WriteFileAtomically` | folder made, `<path>.tmp` written, flushed, closed, renamed over the target |
| `PathToUtf8(path)` (`FilePath.h`) | the UTF-8 `String` of a `std::filesystem::path`: the one `u8string()` call, so `File.h` stays free of `<filesystem>` |
| `Path` (`Path.h`) | a UTF-8 path over `String` and the view helpers it copies (`PathParent`, `PathFilename`, `PathStem`, `PathExtension`, `PathIsAbsolute`, `PathRelativeTo`, `PathCompare`, `PathHash`); `operator/`, `LexicallyNormal`, `ReplaceExtension` |
| `TextFileWriter<N>` (`TextFileWriter.h`) | text into an inline buffer of `N` bytes over a `File`, for a writer with nowhere to return an error (a log sink): `Append`, `AppendFormat` (cut at `N`, never allocated), `Flush`, `Close`; the first write error is kept (`HasError`, `TakeError`) and later calls do nothing; no allocation after `Open` |
| `TextReader` (`TextReader.h`) | lines (`\n` or `\r\n`), white-space tokens and whole-token numbers (`ReadInt<T>`, `ReadFloat<T>` through `std::from_chars`) over a `StringView` it does not own; nothing allocates, a bad or out-of-range token leaves the position |

## Contracts

- **A path is a UTF-8 real path taken as it is**, never a virtual one: resolve with `FileSystem::ResolvePath` first. A path from `std::filesystem` goes in as `u8string()`, not `string()` (the ANSI code page on Windows).
- **Errors** are `std::expected<T, String>` with a sentence for the log. `Open` and the whole-file calls name the path; the calls on an open `File` name only SDL's error (the file keeps no path, that would allocate), so the caller adds the path. Read `SDL_GetError` right after the failed call: it is thread-local and the next SDL call overwrites it.
- **Every mode is binary**: no line-end translation. `Write`, `Append` and `CreateNew` do not make the folder above.
- **`Size()` comes from the stream**, not from the path: on Android `SDL_IOFromFile` falls back to the APK assets, which `GetFileInfo` does not know. The whole-file reads use it for that reason and take the limit check before the allocation, because the size of a file is untrusted input.
- **A scratch read has a bound, and a bigger file goes to the heap:** the chunks of a scratch arena stay allocated after the scope ends, so one big file would stay with the thread for good. The caller names the bound for its site (`ConfigManager`: `kMaxScratchConfigBytes`); `maxBytes` is still checked before the arena is asked. The bytes live as long as the scope (no `co_await` in it); `IsOnHeap()` tells which path was taken.
- **`RemoveFile` of a missing path is an error** (SDL's `SDL_RemovePath` succeeds): on success the file was there and is gone. A folder is refused too.
- **`WriteFileAtomically`** removes its temp file on every failure after it was opened, closed first (Windows cannot delete an open file); a temp name that cannot be opened (a folder) is left alone. The old target is untouched until the rename. There is no `fsync`: it is about a failed write, not durability against a power loss.
- **`Path` follows `std::filesystem::path` component by component** (the table of `PathTest` is checked against it on POSIX): `Filename("a/b/")` is empty, `Stem(".hidden")` is `.hidden`, `Extension("a.b.c")` is `.c`, `a//b == a/b` and the hash agrees, `a` comes before `a/`. The text is stored as written, except that on Windows a `\` becomes `/` (what `generic_string()` gave); a backslash is a name character elsewhere. The helpers return views of the argument and allocate nothing. A virtual path (`mount:/...`) is not a `Path`: `FileSystem::ResolvePath` first.
- **The calls of `File.h` take `StringView` and return `String`**, so the header needs no `<filesystem>`: a `Path` goes in as it is (`operator StringView`) and a result is wrapped with `Path(String&&)`. `Path.h` includes `<filesystem>` for the **transitional conversions** to and from `std::filesystem::path` (marked `//-NoStd`); they are the only place the two types meet and the last stage of the entry `engine-path-type` removes them with `PathToUtf8`.
- **`WalkDirectory` lists in the OS's order** (sort what you collect). A subfolder that cannot be read is skipped, the top one is the error. A symbolic link (a reparse point on Windows) to a folder is reported with the kind of its target, an unreadable one as `Other`, and is not entered in `Tree` mode; the depth is bound (`kMaxWalkDepth`, 64). `visit` must not change the folder it is listing.
- **`RemoveTree` never follows a link**: a link inside is removed as a link and its target stays; a missing path is an error, like `RemoveFile`. **`CopyFileTo`** refuses a folder on either side, the same file (SDL truncates the target before it reads) and, in `Fail` mode, an existing target (not race-free); `Overwrite` is in place, so a failed copy leaves a partial file. It is not called `CopyFile`: windows.h defines that macro.
- **Canonical forms:** `CanonicalPath` needs the path to exist (an error that names it); `WeaklyCanonicalPath` resolves the longest existing part with its links (`link/..` leaves the link's target, as std does) and only cleans up the rest. `AbsolutePath` is lexical and resolves no link. `IsSameFile` is an error for a missing path. On macOS the temp folder is under the link `/var`: compare with `CanonicalPath`, not with the spelled path.

## Traps

- On macOS and Linux `SDL_IOFromFile` is `fopen`: libc's `FILE` and its buffer are invisible to the tracker; only the layer's own allocations are counted (the tests pin the whole-file reads to under twice the file size, not an allocation count).
- SDL opens regular files only (a folder or a pipe fails); there is **no exclusive-create mode**, so `FileMode::CreateNew` is "`GetFileInfo` says missing, then open for write": two processes can both pass the check.
- Windows opens for writing with share mode 0 (nobody reads the file meanwhile) and for reading with `FILE_SHARE_READ` only (it fails while another handle writes). `RenameFile` is `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` there and `rename` elsewhere: it replaces a file and fails when the target is a folder.
- `SDL_CreateDirectory` makes the parents and succeeds when the folder exists. On POSIX it is `mkdir(0770)`, not `0777 & ~umask` like `std::filesystem`: a folder made through it is closed to others whatever the umask.
- `GetFileInfo` allocates nothing on POSIX; on Windows SDL converts the path to UTF-16 with `SDL_malloc`, which the tracker counts (the test is skipped there).
- `TextFileWriter` cuts a formatted line longer than `N`; `XmlDocument::SaveToFile`, `XmlSerializer::SaveToFile` and the other `Write`-mode users truncate the file in place, so a watcher of the folder sees no `<name>.tmp` (an atomic write would show two changed files).
- `BECore::Tests::ReadFileBytes` (`tests/Common/TestFiles.h`) hides the layer's `ReadFileBytes` inside `namespace BECore::Tests`: qualify the layer's call.

## Tests

`tests/BECore/PathTest.cpp` (`Path`, a table checked against `std::filesystem` on POSIX), `tests/BECore/FileDirectoryTest.cpp` (walk, tree removal, copy, current folder, absolute and canonical forms, links), `tests/BECore/FileTest.cpp` (the stream and the path calls, a non-ASCII and a long path), `tests/BECore/FileReadWriteTest.cpp` (the whole-file calls and the atomic write, its failure cases), `tests/BECore/TextFileWriterTest.cpp`, `tests/BECore/TextReaderTest.cpp`, `tests/BECore/StringXmlWriterTest.cpp` (in `Config/`). The allocation checks run only in `macos-profile`. Not run on Windows, the web or Android yet: the UTF-8 paths, the share modes, the asset size and the Windows branches of the calls above (`GetFinalPathNameByHandleW`, reparse points, `\` to `/`) are checked there by hand.
