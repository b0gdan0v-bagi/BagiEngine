# Editor polish: asset browser, Build / Export, feedback, game panels, inspector cards and workspaces

- Version: 0.30.0
- Entry: editor-polish
- Date: 2026-11-26
- Bump: minor
- Content version: 6

## Added
- Asset browser with previews and drag-and-drop of an asset into the inspector.
- Build / Export of a game from the editor, desktop and web.
- Editor feedback: toasts, progress and motion; changes made through the control API flash and are marked in the editor, and the control API reaches the active scene's nodes by name.
- A game can add its own debug panels and cheat commands to the editor.
- Inspector: component cards with category colours, marks of values that differ from the default and a reset, field help (tooltips and the Field Help panel), drag on a field's name, X/Y/Z axes, selection history (Alt+Left/Right), breadcrumbs, and workspaces Scene / UI / Animation / Profiling.
- An enabled checkbox on a component card: `enabled="false"` on a scene component is saved and the component is created asleep (no update, no drawing) until enabled; `IComponent` stays at 128 bytes.

## Breaking
- `IEditorLayoutHost` has a new pure virtual `GetActiveWorkspace()`. Adapt: a game's own layout host returns its workspace, or an empty `Optional` when it has none.
- Content version 6: `enabled` on a scene component now means the component sleeps. Adapt: run the migration below (it removes `enabled="false"` from the engine's components so they keep running and refuses it on a game's own component type, which the game decides by hand).

## Migration
- `python3 engine/tools/migrate/migrate.py <game folder>`: step `s0005_component_enabled` (5 to 6) removes `enabled="false"` from the engine's scene components and refuses it on a game's own component type.
