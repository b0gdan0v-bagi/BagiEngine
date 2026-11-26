# Asset Browser

The editor's view of the content files. Editor only: the folder is in the `BE_WITH_EDITOR` regex of `src/CMakeLists.txt` (`.claude/rules/widgets-module.md`).

The window is an asset, `config/panels/AssetBrowserPanel.xml` (`Asset Browser###AssetBrowser`, hidden at start and in no default dock, `view.assetBrowser`; the format: `src/Widgets/PanelAssets/CLAUDE.md`), over `AssetBrowserPanelModel` and the slots of `AssetBrowserSlots`: the toolbar, the line of the selected file and the empty state are the asset's, the folder tree (`assetFolders`) and the grid (`assetGrid`) are C++.

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

## Window and slots

`AssetBrowserSlots` (ImGui, a member of the model; `AssetBrowserAssetTest` draws the shipped asset over a content root of its own).

- **A slot notes, the model applies.** A click in the tree or on a tile goes to `NoteFolderClick` / `NoteAssetClick` and is applied in `EndUpdate`, so the shown list never changes while it is walked.
- **The tree** starts with a root row, "All content" (the empty folder path), above the mounts; the root and the mounts start open. A tree node's ImGui id is the folder's path.
- **Only a tile in view loads a texture**: the grid is clipped by rows, and a tile outside the clip rect draws and loads nothing. The thumbnails are a `SpriteTextureCache` (an evicted texture is loaded again; a failed load is remembered and shows a warning icon until the catalog is made again, by a file change or Rescan). Closing the panel lets every thumbnail go. A tile's id is the file's path under the label `##<file name>`, which the control API finds it by.

## Drag and drop

`AssetDrag.{h,cpp}` is the whole contract: a panel draws a source with `DrawAssetDragSource` and takes a drop with `AcceptAssetDrop`, and none handles the ImGui payload (`BE_ASSET`) itself (`AssetDropInspectorTest`).

- **Sources.** A tile of the grid, of a draggable kind (`IsDraggableAsset`): `Texture`, `Font`, `Sound`, `UiDocument`, `Node`. A path that does not fit the payload starts no drag, because a cut path would name another file.
- **Targets** name the kinds they take and no disabled item is one, so a read-only field and a locked node take nothing and show no highlight. A delivered payload is handed out as a copy: ImGui frees its bytes when the target ends.
- **An inspector field** takes a file when it is a string marked `BE_FIELD_ATTRS(asset(<Kind>))` (a game's field the same way; `reflection-module.md`): `ImGuiPropertyVisitor` writes the virtual path. An unmarked string takes nothing, since a dropped path would silently spoil a free text. A custom inspector asks for the drop itself, as the sprite renderer's texture combo does (not while a sprite id locks it).
- **The viewport** takes the kinds a scene makes a node of (`MakesSceneNode`: `Texture`, `Font`, `Sound`, `Node`; `widgets-module.md`, "Scene Editor Conventions"). The others have no one obvious component.
