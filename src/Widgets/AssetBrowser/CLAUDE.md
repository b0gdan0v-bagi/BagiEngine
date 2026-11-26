# Asset Browser

The editor's view of the content files. Editor only: the folder is in the `BE_WITH_EDITOR` regex of `src/CMakeLists.txt` (`.claude/rules/widgets-module.md`).

## Catalog

`AssetCatalog` (no ImGui, no `CoreManager`, works over any `FileSystem`; `AssetCatalogTest`) holds what the last `Rebuild` found.

- **What is listed.** The files of the mounts `assets` and `config`, every layer, a file of several layers once, whose extension is in the table `kAssetExtensions` (`AssetCatalog.cpp`; the listing and `AssetKindOfName`, which the file watch asks, both read it, so they agree). Anything else (`.ini`, `.bin`, licence texts, temp files) does not exist for the browser, and neither does a `*.artgen.json` sidecar. A new asset extension is a row of the table.
- **Kinds** (`AssetKind`, `BECore/Resource/AssetKind.h`). The name decides, `.ui.xml` before `.xml`. A `.xml` under `config` is told by the file in its top layer: `SceneConfig.xml` is `Scene`, a document element `<node>` is `Node`, anything else, an unreadable file included, is `Config`. A `.xml` under `assets` is `Other`.
- **Files are read only in `Rebuild`** (the XML under `config`); every other call works on what it kept.
- **Folders** are those that hold a listed file at some depth, so an empty folder is not shown; a parent comes before its children (`PathCompare` order). The empty path is the root above the mounts.
- **Shown files** (`CollectShown`): with no search and the filter `All`, the files directly in the folder; otherwise every match at any depth below it. The search is a substring of the file name, never of the folders, with ASCII case ignored.
