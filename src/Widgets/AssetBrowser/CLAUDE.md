# Asset Browser

The editor's view of the content files. Editor only: the folder is in the `BE_WITH_EDITOR` regex of `src/CMakeLists.txt` (`.claude/rules/widgets-module.md`).

The window is an asset, `config/panels/AssetBrowserPanel.xml` (`Asset Browser###AssetBrowser`, hidden at start, docked by the ready layouts of the UI and Animation workspaces, `view.assetBrowser`; the format: `src/Widgets/PanelAssets/CLAUDE.md`), over `AssetBrowserPanelModel` and the slots of `AssetBrowserSlots`: the toolbar, the line of the selected file and the empty state are the asset's, the folder tree (`assetFolders`) and the grid (`assetGrid`) are C++.

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

## Who uses a file

`FindAssetReferences` (`AssetReferences.{h,cpp}`, no ImGui; `AssetReferencesTest`) answers with (file, line) pairs, from the saved files only. A missed use lets the editor break content silently, so every doubt counts as a use.

- **Sources.** The `.xml`, `.html`, `.rml`, `.css`, `.rcss` and `.json` files of the mounts `assets` and `config`, each as its top layer has it, and the `.json` files below `<GetRootPath()>/artgen/` (presets and sets, which name pictures from the content root). The asset itself and `*.artgen.json` sidecars (records of a generation) are no users.
- **A use** is the whole virtual path standing on its own (never a piece of a longer path), ASCII case ignored and `\` read as `/`, because macOS and Windows open either; XML references and JSON escapes are decoded; a comment of markup or of a style sheet is not read. A path with `./` or `..` steps counts, and in HTML, RML and style files also a link relative to the file's folder.
- **A node file** is also used by its reference: `ref="nodes/X"`, and, when the reference has a folder, the text `nodes/X` anywhere (a game's component may hold one).
- **Could not be checked** (`unchecked`, never "unused"): a file that cannot be read or holds a zero byte (UTF-16), a folder that cannot be listed, and a linked folder, which no listing of content enters.
- **Never seen:** names written or built in C++, cooked `.bin` content, files outside these roots, and a sprite id or a font family, which use the library's or `TextConfig`'s entry (that entry is the use of the file).

## The content check

`CheckContentAssets` (`AssetReferences.{h,cpp}`; `AssetReferencesTest`) reads each source once and answers `missing`, `unused` and `unchecked` (as above; a `SceneConfig.xml` that does not parse is one too). Here the doubt goes the other way: a name the check cannot be sure of is not reported as missing.

- **Missing, a path.** In the mount sources (not `artgen/`: its paths start at the content root and are not shipped), a path that starts with `assets/` or `config/`, ends in a listed extension and that no layer has, ASCII case ignored; `config/X.xml` is there when `X.bin` is. A name with a space is read only when it is a whole quoted, bracketed or element text. Not judged: comments (those of an embedded `<style>` too), a piece of a longer path or of a URL, folders, unlisted extensions, patterns (`*`, `%`, `$`, `<name>`), relative links and links that start with `/`. A text that shows an example path therefore writes it as a pattern.
- **Missing, a node.** A `ref` of a `<node>` element of a scene or node file that is no valid reference or has no `config/<ref>.xml` or `.bin`; the line is the element's.
- **Unused.** A listed file of a kind that may be deleted ("Delete and rename"), a file of the content root itself by the same rule (`ResolveContentRootFile`), that no source uses by "Who uses a file". So the engine's files are never a game's unused ones, and a file only code names is listed.

## Delete and rename

`AssetFileOps.{h,cpp}` (no ImGui; `AssetFileOpsTest`) is the one place that decides and does it. It destroys and moves the owner's files, so a case it cannot be sure of is refused, with a sentence for the user and the disk unchanged. A delete is permanent.

- **Which file** (`ResolveContentRootFile`). An asset of a mount point whose top file lies in that mount folder of `GetRootPath()` itself: a game never changes the engine's file, although the engine's checkout lies below the game's root. No symbolic link between the root and the file. Never with a content overlay, which cannot express a removal.
- **Which kinds.** `Texture`, `Font`, `Sound`, `UiDocument`, `Node` and `Other`. A config and a scene file stay: code reads them by name, which no reference check sees.
- **A new name** replaces the stem; the folder and the extension (`.ui.xml` and `.ui.html` whole, `AssetRenameStem`) stay. `A-Z a-z 0-9 _ -`, single inner dots too except for a node file, whose reference must stay valid. Refused: a name any layer has (ASCII case ignored, so content stays portable), one that differs from the old only by case, one that makes another kind of file, and a node name some layer holds cooked.
- **The lower layer.** The answer says when the old path still leads somewhere (`lowerLayerShows`): the engine's file, or a node's cooked form.
- A sidecar (`*.artgen.json`) stays where it is, and nothing rewrites the content that names the old path.

## Delete and rename in the panel

The toolbar's **Rename** and **Delete** act on the selected file through a modal of the asset (`assetBrowser.rename|delete|confirm|cancel`; `AssetBrowserPanelModelTest`).

- **Off with the reason as the tooltip** while the file rules refuse it or any editor document is unsaved (`GetEditorDocuments`): the reference check reads saved files, and a later Save would write the old path back.
- **When that is decided.** It reads the disk and `Collect()` allocates, so never per frame: on a new selection or catalog, after any editor command and when the pointer comes back over the panel (the tree slot says where it is). A button can therefore be stale for a while; the command and the confirm check again.
- **The dialog** reads the users when it opens and lists them; with users, or files that could not be checked, the button says "anyway". Nothing is rewritten.
- **Confirm** acts only on what the dialog showed: an unsaved document, a refusal, a changed file or a changed list of users is said in the dialog, which then shows the new state and waits for another confirm. Closing the panel drops the question.
- **After it** the old path is evicted from the resource cache, the files are listed again and the renamed file is selected; a failure, or a file of a lower layer that shows through, is a line in the panel.
