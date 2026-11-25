# Editor: the control API reports the unsaved documents of the editor panels; --content-overlay redirects content writes

- Version: 0.25.1
- Entry: editor-dirty-state-api
- Date: 2026-11-25
- Bump: patch

## Added
- Control method `editor_state`: the documents of the editor panels (scene, node files of pending UI edits, the selected config, the Sprite Library) with their `dirty` flag, virtual `path` and the real `file` a save writes (`.claude/rules/control-editor-api.md`); `GameSession.editor_state()` and an MCP tool mirror it. A panel registers its documents through `CoreManager::GetEditorDocuments()`. Adapt: nothing; a game's own editor panel may register its documents the same way.
- Start flag `--content-overlay=<dir>`: every write through `ConfigManager::SaveConfig`, `UINodeFileSession::Save` and `FileSystem::GetMountedPath` goes into `<dir>` instead of the layer the file came from (`FileSystem/CLAUDE.md`, "Content overlay"). Adapt: nothing; tools that let the editor save should start it with an overlay.
