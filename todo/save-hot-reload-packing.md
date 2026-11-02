---
stage: 5
id: no-save-games-hot-reload-or-asset-packing
---
# No save games, hot-reload or asset packing

**Where:** `src/Modules/BECore/Reflection/BinarySerializer.h` / `BinaryDeserializer.h` (format in `.claude/rules/reflection-module.md`), `src/Modules/BECore/FileSystem/`, `Resource/`.

**What is missing** (found by reading the code, not by running a game):

1. `BinarySerializer` has no consumer: a game cannot save and restore its state.
2. Changing a texture or config needs a restart; there is no file watcher.
3. Assets are loose files, with no packed format for a release build.

**A fix has to cover:**

- a save game service built on `BinarySerializer` (versioned, name-keyed as the format already is) with a Chess or Match3 model as the first consumer;
- hot-reload of textures and configs in Editor mode through `FileSystem` and `ConfigManager::ReloadConfig`;
- later: a pak file behind the virtual `FileSystem` mount points, so a release reads packed assets through the same paths.
