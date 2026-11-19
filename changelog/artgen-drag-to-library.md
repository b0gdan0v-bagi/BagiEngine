# Editor: a generated variant cannot be dragged from the Generate panel into the Texture library or other panels

- Version: 0.6.2
- Entry: artgen-drag-to-library
- Date: 2026-11-19
- Bump: patch

## Added
- A variant of the Generate tab or a cell of the Set tab can be dragged into the Sprite Library: a drop on a row replaces that sprite, a drop on empty space opens **Add sprite** with a name. The payload and the rules a panel follows to take a drop are `ArtGenDrag.h`.
