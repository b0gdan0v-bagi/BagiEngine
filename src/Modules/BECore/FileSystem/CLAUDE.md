# FileSystem Module Guidelines

## Overview

`FileSystem` maps virtual paths to real ones through named mount points. Engine code and XML configs only ever use virtual paths such as `config/LoggerConfig.xml` or `assets/chess_test.png`; accessor: `CoreManager::GetFileSystem()`.

## Content roots and layers

Content is layered. A **content root** is a directory with `config/` and/or `assets/` inside; roots are ordered from lowest to highest priority. Every game executable has two: the engine checkout (`BAGI_ENGINE_ROOT`) and the game (`BAGI_GAME_ROOT`) on top of it. A mount point (`config`, `assets`) has one **layer** per root that has such a folder, and a lookup goes through the layers from the top: a game file overrides an engine file of the same relative path. Files that exist in one layer only are seen as they are.

`main.cpp` adds the two roots (`AddContentRoot`, engine first) before `Application::Initialize()`; `be_add_game_executable` defines the macros. The gtest environment adds the engine root only.

## Initialization

`Initialize()` (first real service in `InitializeCoreServices()`):
1. With no content root added, finds the project root: starting at the current working directory, walks up at most 10 levels to the first directory that contains a `config/` folder (falls back to the working directory) and uses it as the only root.
2. Mounts `config` and `assets` of every root, lowest priority first, when the folders exist.

Without the macros (a target not built through `be_add_game_executable`) the game and the test executable must be started from inside the repository tree, even when the build directory is elsewhere.

## API

| Call | Notes |
|------|-------|
| `ResolvePath(virtualPath)` | Returns an existing real path or an empty path |
| `Exists(virtualPath)` | `ResolvePath` + existence check |
| `AddContentRoot(path)` | Add a root on top of the existing ones (call before `Initialize()`); adding one twice is a no-op |
| `Mount(virtualPath, realPath)` / `Unmount(virtualPath)` | Add a directory (must exist) as the top layer of a mount point / drop the mount point with all its layers |
| `GetMountedPath(mountPoint)` / `GetMountLayers(mountPoint)` | Top layer / all layers, lowest priority first |
| `GetRootPath()` | Highest priority root (the game's) |
| `EnumerateFiles(mountPoint, extensions)` | Recursive listing of all layers of a mount point, filtered by extension, as `PoolString`s; a file present in several layers is listed once |
| `SetRootPath(path)` | Replace all roots with this one |

`ResolvePath` tries, in order: absolute path as given → mount point (first path segment) + remainder, layer by layer from the top → relative to each root, top first → relative to the current directory. A missing file gives an empty path, not an error: check the result.

## Rules

- Never build asset or config paths by concatenating strings or `std::filesystem` calls in engine or game code; pass the virtual path to `FileSystem`, `ResourceManager` or `ConfigManager`.
- Use `PoolString` for mount-point names and `"name"_intern` for literals.
- Writing files (saves, config edits) goes through the resolved real path; do not assume the working directory is the root. An edited engine default is written back into the engine checkout (the layer where it was found), not copied into the game.
- A new file for a game belongs in `games/<Name>/config/` or `games/<Name>/assets/`; a game file with the name of an engine file replaces it as a whole (configs are not merged).
