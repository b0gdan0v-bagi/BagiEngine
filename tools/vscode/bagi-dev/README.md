# Bagi Dev (VS Code)

One tree in the activity bar to build and run the products of a repository on BagiEngine: the game, its tests, the browser build, the room server, the tools. It is a thin face of `engine/tools/dev/dev.py` (`tools/dev/CLAUDE.md`): every button is a `dev.py` command in a terminal, so the terminal and the agents can do the same.

- **Desktop: `<preset>`** (click: pick another) and under it the **flags** of `dev.json` (or the engine's default list: editor, profiler, allocation counting, Tracy, sanitizers, single thread, asserts, RmlUi, ccache). A click switches one: `dev.py flags <preset> --set NAME=ON` writes the presets `<preset>-dev` (configure, build and, when the base has one, test) into `CMakeUserPresets.json`, and the tree picks `<preset>-dev`. The flagged build has its own folder `build/<preset>-dev/`, so it never spoils the plain one; CMake Tools lists it too. The last flag switched back removes the three presets.
- **Web: `<preset>`** (click: pick another): the preset of the products with `"kind": "web"`.
- **Products**: a click (or ▶) runs `dev.py run <product> --preset <picked>`: configure when the build folder has no cache, build the targets, the `after` steps (the web package), then start it in the terminal (a new run replaces the terminal of the old one). 🔧 only builds; the context menu configures again. A product with `"prefer": "release"` (the arena) takes the Release preset unless a preset was picked.
- **Chains**: several products at once (`dev.py chain`), e.g. the room server and two desktop games pointed at it; Ctrl+C in the terminal stops them all.
- **Tools**: the repository's tools (screenshots, the tile atlas check, format, the docs budget); one with `build` builds its product first.

The tree reads `dev.py info --json` at start, on **Refresh** and when `dev.json`, `CMakePresets.json` or `CMakeUserPresets.json` changes. The picked presets are kept per workspace. Settings: `bagiDev.python` (`python3`).

## Install

No npm: the packer uses the standard library.

```bash
python3 engine/tools/vscode/bagi-dev/pack.py
code --install-extension engine/tools/vscode/bagi-dev/build/bagi-dev-0.1.0.vsix
```

Then **Developer: Reload Window**. After a change bump `version` in `package.json`, pack and install again.
