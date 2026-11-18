# Editor: no Generate panel: a sprite cannot go from a prompt to the game (variants, accept, PNG + TextureLibrary + provenance, reload)

- Version: 0.1.18
- Entry: editor-art-generate-panel
- Date: 2026-11-16
- Bump: patch

## Added
- The editor's Generate panel (`view.generate`, View → Generate Art; `src/Widgets/ArtGen/CLAUDE.md`): a style preset of the game and a subject become image variants through the art generation service, and an accepted one becomes the sprite `assets/artgen/<name>.png` with its TextureLibrary entry and provenance, reloaded at once, optionally put on the selected node.
- Control methods `artgen_poll`, `artgen_progress`, `artgen_image`, `artgen_done` (the service's side; it pulls) and `artgen_generate`, `artgen_cancel`, `artgen_accept`, `artgen_state` (an agent's); `tools/artgen/artgen_bridge.py` is the service's client.
- `ResourceManager::Evict(path)` and `ResourceCache::Remove(path)`: one path out of the resource cache, so the next load reads the file again.
- `SceneEditedEvent`: another panel tells the scene editor the active scene is unsaved.
