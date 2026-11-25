# QA tools read the editor's dirty state from editor_state and save into a content overlay

- Version: 0.26.1
- Entry: editor-dirty-state-api
- Merge: 2
- Date: 2026-11-25
- Bump: patch

## Changed
- `QaSession` starts the editor with `--content-overlay` and reads unsaved changes from `editor_state` (`dirty()`, `any_dirty`, `scene_document`, `QaSession.editor_state`); the editor charters read the overlay's saved file and never write the checkout, and the control smoke runs the whole dirty cycle of a real editor. Adapt: nothing; a game's QA charters that read a saved file take it from the overlay (`tools/qa/CLAUDE.md`).
