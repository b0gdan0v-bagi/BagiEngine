# FileSystem Module Guidelines

## Overview

`FileSystem` maps virtual paths to real ones through named mount points. Engine code and XML configs only ever use virtual paths such as `config/LoggerConfig.xml` or `assets/chess_test.png`; accessor: `CoreManager::GetFileSystem()`.

## Initialization

`Initialize()` (first real service in `InitializeCoreServices()`):
1. Finds the project root: starting at the current working directory, walks up at most 10 levels to the first directory that contains a `config/` folder (falls back to the working directory).
2. Mounts `config` and `assets` under that root when the folders exist.

That is why the game and the test executable must be started from inside the repository tree, even when the build directory is elsewhere.

## API

| Call | Notes |
|------|-------|
| `ResolvePath(virtualPath)` | Returns an existing real path or an empty path |
| `Exists(virtualPath)` | `ResolvePath` + existence check |
| `Mount(virtualPath, realPath)` / `Unmount(virtualPath)` | Mount a directory (must exist) under a name |
| `GetMountedPath(mountPoint)` | Real directory of a mount point |
| `EnumerateFiles(mountPoint, extensions)` | Recursive listing of a mount point, filtered by extension, as `PoolString`s |
| `SetRootPath(path)` | Override the discovered root |

`ResolvePath` tries, in order: absolute path as given → mount point (first path segment) + remainder → relative to the root → relative to the current directory. A missing file gives an empty path, not an error: check the result.

## Rules

- Never build asset or config paths by concatenating strings or `std::filesystem` calls in engine or game code; pass the virtual path to `FileSystem`, `ResourceManager` or `ConfigManager`.
- Use `PoolString` for mount-point names and `"name"_intern` for literals.
- Writing files (saves, config edits) goes through the resolved real path; do not assume the working directory is the root.
