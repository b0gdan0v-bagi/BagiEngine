# File Module Guidelines

## Overview

The engine's own file layer over SDL3 (`SDL_IOStream`, `SDL_GetPathInfo`, `SDL_RenamePath`), in `File.h`; `File.cpp` is the only file that includes SDL for it. Why not `std::` streams: no `filebuf`, locale or `std::string`, the layer's own memory goes through the allocation tracker (SDL's allocator is routed to it, `Profiler/Alloc/CLAUDE.md`), and one atomic write replaces the three copies that were written by hand. Nothing in the engine uses the layer yet: the stream users move over in the next stage of the entry `engine-file-layer`, together with `TextFileWriter`, `TextReader` and `StringXmlWriter`. The layer is not in `pch.h`: include `<BECore/File/File.h>`.

| API | Does |
|-----|------|
| `File::Open(path, FileMode)` | one open file, move-only, closed by the destructor; `Read`, `Write`, `Flush`, `Size`, `Close` |
| `GetFileInfo(path)` | `Optional<FileInfo>` (kind, size, mtime); nullopt for a missing or unreadable path; allocates nothing on POSIX |
| `CreateDirectories`, `RemoveFile`, `RenameFile` | folder tree, one file, a rename that replaces a file |
| `ReadFileBytes`, `ReadTextFile` | a whole file in one allocation, refused above `maxBytes` (default `kDefaultMaxFileBytes`) before anything is allocated |
| `WriteFileAtomically` | folder made, `<path>.tmp` written, flushed, closed, renamed over the target |
| `PathToUtf8(path)` (`FilePath.h`) | the UTF-8 `String` of a `std::filesystem::path`: the one `u8string()` call, so `File.h` stays free of `<filesystem>` |
| `TextFileWriter<N>` (`TextFileWriter.h`) | text into an inline buffer of `N` bytes over a `File`, for a writer with nowhere to return an error (a log sink): `Append`, `AppendFormat` (cut at `N`, never allocated), `Flush`, `Close`; the first write error is kept (`HasError`, `TakeError`) and later calls do nothing; no allocation after `Open` |

## Contracts

- **A path is a UTF-8 real path taken as it is**, never a virtual one: resolve with `FileSystem::ResolvePath` first. A path from `std::filesystem` goes in as `u8string()`, not `string()` (the ANSI code page on Windows).
- **Errors** are `std::expected<T, String>` with a sentence for the log. `Open` and the whole-file calls name the path; the calls on an open `File` name only SDL's error (the file keeps no path, that would allocate), so the caller adds the path. Read `SDL_GetError` right after the failed call: it is thread-local and the next SDL call overwrites it.
- **Every mode is binary**: no line-end translation. `Write`, `Append` and `CreateNew` do not make the folder above.
- **`Size()` comes from the stream**, not from the path: on Android `SDL_IOFromFile` falls back to the APK assets, which `GetFileInfo` does not know. The whole-file reads use it for that reason and take the limit check before the allocation, because the size of a file is untrusted input.
- **`RemoveFile` of a missing path is an error** (SDL's `SDL_RemovePath` succeeds): on success the file was there and is gone. A folder is refused too.
- **`WriteFileAtomically`** removes its temp file on every failure after it was opened, closed first (Windows cannot delete an open file); a temp name that cannot be opened (a folder) is left alone. The old target is untouched until the rename. There is no `fsync`: it is about a failed write, not durability against a power loss.

## Traps

- On macOS and Linux `SDL_IOFromFile` is `fopen`: libc's `FILE` and its buffer are invisible to the tracker; only the layer's own allocations are counted (the tests pin the whole-file reads to under twice the file size, not an allocation count).
- SDL opens regular files only (a folder or a pipe fails); there is **no exclusive-create mode**, so `FileMode::CreateNew` is "`GetFileInfo` says missing, then open for write": two processes can both pass the check.
- Windows opens for writing with share mode 0 (nobody reads the file meanwhile) and for reading with `FILE_SHARE_READ` only (it fails while another handle writes). `RenameFile` is `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` there and `rename` elsewhere: it replaces a file and fails when the target is a folder.
- `SDL_CreateDirectory` makes the parents and succeeds when the folder exists. On POSIX it is `mkdir(0770)`, not `0777 & ~umask` like `std::filesystem`: a folder made through it is closed to others whatever the umask.
- `GetFileInfo` allocates nothing on POSIX; on Windows SDL converts the path to UTF-16 with `SDL_malloc`, which the tracker counts (the test is skipped there).
- `BECore::Tests::ReadFileBytes` (`tests/Common/TestFiles.h`) hides the layer's `ReadFileBytes` inside `namespace BECore::Tests`: qualify the layer's call.

## Tests

`tests/BECore/FileTest.cpp` (the stream and the path calls, a non-ASCII and a long path), `tests/BECore/FileReadWriteTest.cpp` (the whole-file calls and the atomic write, its failure cases). The allocation checks run only in `macos-profile`. Not run on Windows, the web or Android yet: the UTF-8 paths, the share modes and the asset size are checked there by hand.
