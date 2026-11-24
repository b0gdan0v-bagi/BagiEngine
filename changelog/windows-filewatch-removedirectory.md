# Windows file watch: BECoreModule does not compile (the windows.h macro RemoveDirectory) and Shutdown hangs

- Version: 0.9.1
- Entry: windows-filewatch-removedirectory
- Date: 2026-11-23
- Bump: patch

## Changed
- `WindowsFileWatchBackend.cpp` undefines the `windows.h` macro `RemoveDirectory` after the include, so its override of `IFileWatchBackend::RemoveDirectory` is not renamed to `RemoveDirectoryW`: `BECoreModule` compiles on Windows again (broken since `file-watcher`).
  Adapt: nothing; a game's own code that includes `windows.h` and names a method `RemoveDirectory` undefines the macro the same way.
- The Windows file watch backend's `Shutdown` no longer hangs: it cancels every pending read and waits until the reader thread took each completion before it stops the thread (it waited on the directory handle, which a read completed through the port does not signal). A Debug build of a game on Windows (`fileWatch` `Auto` watches there) no longer hangs at exit.
  Adapt: nothing.
