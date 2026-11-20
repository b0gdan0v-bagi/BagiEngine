# FileSystem Module Guidelines

## Overview

`FileSystem` maps virtual paths to real ones through named mount points. Engine code and XML configs only ever use virtual paths such as `config/LoggerConfig.xml` or `assets/board.png`; accessor: `CoreManager::GetFileSystem()`.

## Content roots and layers

Content is layered. A **content root** is a directory with `config/` and/or `assets/` inside; roots are ordered from lowest to highest priority. Every desktop game executable has two: the engine checkout (`BagiContentRoots::kEngine`) and the game (`kGame`) on top of it. The web build has a third, `BAGI_WEB_ROOT`: the engine's `web/` overlay (`web/config/`: window, renderer and log for the browser), on top of the game, so its files win over the game's of the same name (a layer replaces a whole file). A mount point (`config`, `assets`) has one **layer** per root that has such a folder, and a lookup goes through the layers from the top: a game file overrides an engine file of the same relative path. Files that exist in one layer only are seen as they are.

`main.cpp` adds the roots (`AddContentRoot`, engine first, game, then `BAGI_WEB_ROOT` when it is defined) before `Application::Initialize()`. On the desktop the roots are the paths of the checkouts, defined in one generated source per target (`be_link_content_roots`; a `-D` define would put the checkout path into every object's cache key). On the web (`be_web_configure_target`, `cmake/BagiWeb.cmake`) the macros `BAGI_ENGINE_ROOT` / `BAGI_GAME_ROOT` / `BAGI_WEB_ROOT` are virtual paths inside the preloaded package (`/bagi/engine`, `/bagi/game`, `/bagi/web`), where the `config/` and `assets/` folders of each root are packed. The gtest environment adds the engine root only.

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
- Use `PoolString` for mount-point names; the engine's own are `MountPoints::assets` / `config` (`MountPoints.h`), a literal is `"name"_intern`.
- Writing files (saves, config edits) goes through the resolved real path; do not assume the working directory is the root. An edited engine default is written back into the engine checkout (the layer where it was found), not copied into the game.
- Saves are written into a mounted folder (`saves`) by `SaveGameService` (`Save/CLAUDE.md`), not by hand.
- A new file for a game belongs in the `config/` or `assets/` of the game's repository; a game file with the name of an engine file replaces it as a whole (configs are not merged).

## FileWatcher

`FileWatcher` (`FileWatcher.h`, one per `CoreManager`, built over the `FileSystem` that resolves the virtual paths) tells the game that a file or a folder changed on disk. `WatchFile(virtualPath, cb)` and `WatchFolder(virtualFolder, cb)` (recursive) return a move-only `FileWatchHandle`; the watch ends when the handle dies or `Reset()`s. The callback gets a `FileChange{virtualPath}`: always the **virtual** path, one kind of change ("changed"); a callback that must tell a removal from a write asks `FileSystem::Exists`.

- **Main thread only**, for the calls and for every callback. A backend may post from its own thread (`IFileWatchSink::Post`): the post goes into a queue under one lock, `Update()` (once per frame) takes it, so a quiet frame takes no lock and allocates nothing. A handle destroyed inside any callback is safe: the watch stops at once, the removal from the backend waits for the end of `Update()`. `Stop()` is not for a callback. A handle must not outlive the watcher.
- **The setting.** `fileWatch` of `ApplicationConfig.xml` is `Auto`, `On` or `Off` (`FileWatchSetting.h`, default `Auto`). `Auto` watches in the Editor run mode **or** in a build with asserts on (the Debug builds: nobody edits files next to a Release game); `On` and `Off` are as written; the web never watches. `ShouldWatchFiles` is the pure rule; `Application::Initialize` applies it after the run mode is known and before `OnApplicationInit` (`CoreManager::StartFileWatching`) and logs one line.
- **Not running** (no `Start`, the web, the setting off): `Watch*` gives an inactive handle (`IsActive()` false) and `Update()` returns at once. A path that is not under a mount point, or whose folder exists in no layer, is inactive too: a folder that appears later is not picked up.
- **Layers.** A path is watched in **every** layer of its mount point that has the folder, and reported by its virtual path; a file changed in two layers is one notification.
- **Trap: a file watch watches the parent directory** and the facade filters by file name. An editor saves through a temp file and a rename over the target, which makes no event on the replaced file itself. A directory several watches share is added to the backend once (recursive while any user is a folder watch).
- **Coalescing.** One pending entry per (watch, virtual path); every post moves its time, and it is delivered once the path was quiet for `kFileWatchSettleSeconds`. The time of a post is the time of the `Update` that took it. `Update(nowSeconds)` takes the clock as a parameter, so a test drives the window without a sleep (`FileWatcherTest`).
- **Backends** (`IFileWatchBackend`) watch directories and report real paths: `AddDirectory` / `RemoveDirectory` / `SetSink` / `Tick(nowSeconds)` / `Shutdown`. `FakeFileWatchBackend` (header only) is the test one: `Emit(realPath)` from any thread, the added and removed directories recorded.
- **Polling backend** (`PollingFileWatchBackend`, the fallback and the reference for the contract): no thread, it looks from `Tick` (so from `Update`, on the main thread) every `kPollIntervalSeconds`. It keeps the write time and size of each file and the write time of each folder, so a poll that finds nothing changed stats and allocates nothing, and a changed folder is listed again. It cannot see a rewrite that keeps both the write time and the size, nor a file made and removed between two looks. `MakePlatformFileWatchBackend()` (`PlatformFileWatchBackend.h`) gives the backend of the platform, null on the web. The contract cases of every backend are one typed suite (`FileWatchBackendTest`); a native backend is added to its list.
- **FSEvents backend** (macOS, `FSEventsFileWatchBackend.cpp`, returned by the factory on Apple): one `FSEventStream` for the whole set of directories on a serial dispatch queue (no thread of ours), rebuilt on `AddDirectory` / `RemoveDirectory` (the new stream starts before the old one stops). `Shutdown` stops, invalidates and waits on the queue before the callback's snapshot goes, so no post comes after it. **Trap: FSEvents reports real paths** (`/private/var/...` for `/var/...`, symlinks resolved), so each directory keeps its canonical form for matching and the reported path is rebuilt in the form the facade added; an event for the directory itself is dropped, a non-recursive directory drops deeper paths (FSEvents always watches the subtree). A backend that lost events (FSEvents `MustScanSubDirs`, inotify overflow, a Windows burst that did not fit) posts every file below the root with `PostTree`: the facade routes files, so posting the folder itself would tell it nothing. The library `CoreServices` is linked from `BECore/CMakeLists.txt`.
- **inotify backend** (Linux, `InotifyFileWatchBackend.cpp`) and **ReadDirectoryChangesW backend** (Windows, `WindowsFileWatchBackend.cpp`): each starts one reader `Thread("FileWatch")` at the first `AddDirectory` and posts real paths from it. inotify is not recursive, so a recursive directory gets a watch per subfolder (a symlink is not followed), a folder made later is watched when its `CREATE` arrives and its contents are posted, and `IN_Q_OVERFLOW` posts the watched roots; the thread is woken to stop by an `eventfd`. Windows uses one I/O completion port: a stop is a packet without an operation, a completion with 0 bytes (overflow) posts the root, and `RemoveDirectory` cancels the read and waits for its completion before the buffer, whose address is the completion key, goes. **Not built or run on the machine of the author** (macOS only): the first build on Linux and on Windows runs the typed suite of `FileWatchBackendTest`, where a failure is a bug of the backend, not of the facade.
