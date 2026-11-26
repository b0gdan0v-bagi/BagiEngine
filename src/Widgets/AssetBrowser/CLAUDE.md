# Asset Browser

The editor's view of the content files. Editor only: the folder is in the `BE_WITH_EDITOR` regex of `src/CMakeLists.txt` (`.claude/rules/widgets-module.md`).

## Catalog

`AssetCatalog` (no ImGui, no `CoreManager`, works over any `FileSystem`; `AssetCatalogTest`) holds what the last `Rebuild` found.

- **What is listed.** The files of the mounts `assets` and `config`, every layer, a file of several layers once, whose extension is in the table `kAssetExtensions` (`AssetCatalog.cpp`; the listing and `AssetKindOfName`, which the file watch asks, both read it, so they agree). Anything else (`.ini`, `.bin`, licence texts, temp files) does not exist for the browser, and neither does a `*.artgen.json` sidecar. A new asset extension is a row of the table.
- **Kinds** (`AssetKind`, `BECore/Resource/AssetKind.h`). The name decides, `.ui.xml` before `.xml`. A `.xml` under `config` is told by the file in its top layer: `SceneConfig.xml` is `Scene`, a document element `<node>` is `Node`, anything else, an unreadable file included, is `Config`. A `.xml` under `assets` is `Other`.
- **Files are read only in `Rebuild`** (the XML under `config`); every other call works on what it kept.
- **Folders** are those that hold a listed file at some depth, so an empty folder is not shown; a parent comes before its children (`PathCompare` order). The empty path is the root above the mounts.
- **Shown files** (`CollectShown`): with no search and the filter `All`, the files directly in the folder; otherwise every match at any depth below it. The search is a substring of the file name, never of the folders, with ASCII case ignored.

## Model and refresh

`AssetBrowserPanelModel` (no ImGui; `AssetBrowserPanelModelTest`) holds the `AssetBrowserView` the panel binds to, the catalog, the shown files and the selection. Its seams (`SetFileSystem`, `SetWatcher`, `SetEviction`) are set before `Initialize`.

- **Rebuilt only on a cause.** The catalog at the first `Update`, after a watch notice and on `assetBrowser.rescan`; the shown list when the catalog, the folder, the search or the filter moved. A steady `Update` allocates nothing.
- **A notice only marks.** The watches (consumer `AssetBrowser`, both mounts) never rebuild: the next `Update` does, once for however many files changed, and a closed panel, which is not updated, rebuilds nothing. The one thing done in the callback is the eviction of a changed or removed texture from the resource cache, because the cache has other users than an open browser. A path that is neither listed nor a folder (`imgui.ini`, a `.tmp`) is ignored; a folder reported as itself (a rename or a move) marks the catalog and evicts the textures that were under it.
- **After a rebuild** a selected file that is gone is deselected, and a selected folder that is gone becomes its nearest parent that is left.
- **No watcher** (`FileSystem/CLAUDE.md`, "FileWatcher": the web, `fileWatch="Off"`, or `Auto` in a Release build that started in Game mode and was switched to the editor): the watches are inactive and **Rescan** is the way to see a change. It lists the files again and evicts nothing, so a texture rewritten meanwhile stays the cached picture.
