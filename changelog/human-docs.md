# README as the front page for a person; room-server hosting moved to tools/rooms/README.md

- Version: 0.30.4
- Entry: human-docs
- Date: 2026-11-27
- Bump: patch

## Changed
- `README.md` is the engine's front page for a person: what it is, what it has, its principles, how to build it on macOS and Windows (with the Jinja2 the reflection generator needs) and links. Adapt: nothing.
- The room-server hosting text (Docker image, settings, Railway, prebuilt deploy) moved word for word from `README.md` to `tools/rooms/README.md`; `cmake/CLAUDE.md`, `src/Modules/RoomServer/CLAUDE.md`, `tools/rooms/CLAUDE.md` and `tools/rooms/Dockerfile` point there. Adapt: a game's docs that linked the README's hosting section link `engine/tools/rooms/README.md`.
