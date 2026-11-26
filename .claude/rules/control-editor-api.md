---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/BECore/Widgets/**/*"
  - "src/Widgets/**/*"
  - "tools/control/**/*"
---

# `editor_state`: the unsaved changes of the editor panels

`RegisterEditorStateControl` (`BECore/Widgets/EditorStateControl.h`, registered by `CoreManager`) answers

```json
{"anyDirty": false, "documents": [{"id": "scene:Main", "kind": "scene", "name": "Main", "dirty": false, "path": "config/SceneConfig.xml", "file": "/abs/config/SceneConfig.xml"}]}
```

sorted by `id`. It is a method of its own and not a field of `status`: `status` is built in BECore and knows no widget. In the Game run mode (`status.runMode`) it fails with `not_editor`; the Editor mode with no panel open answers an empty list.

- **Documents.** A panel registers a source with `CoreManager::GetEditorDocuments().Add(...)` and keeps the returned handle as a member, so the source ends with the widget (a mode switch destroys the panels). A source reports the panel's own flags, not drawn state: a closed panel keeps reporting. Two sources with the same `id` give one row (the earlier source wins). Main thread only.
- **`kind`** is `scene` (the active scene, `SceneConfig`; `id` `scene:<name>`), `nodeFile` (one per node file the pending UI rect edits would patch, `UINodeFileSession`; always dirty), `config` (the selected config of the Config Editor, from `ConfigEditorPanelModel`, `config:<name>`; dirty = unsaved edits), `library` (the Sprite Library, from `SpriteLibraryPanelModel`, `library:sprites`). `dirty` is the flag the panel's own Save button uses.
- **`path` against `file`.** `path` is the virtual path (`config/SceneConfig.xml`); `file` is the real absolute file a save writes: `FileSystem::WriteTarget` of the resolved file, so with `--content-overlay=<dir>` it is the overlay's file even before the first save, and the checkout's otherwise.
- **`--content-overlay`** (`FileSystem/CLAUDE.md`, "Content overlay"; `control-startup.md`) makes every write of these panels go into a copy. QA starts the editor with it, so a session that saves never changes a tracked content file, and compares `file` with the checkout's.
