# Web editor: the editor in a browser page, the engine's demo page, and the desktop editor attached to a browser game

- Version: 0.30.1
- Entry: web-editor
- Date: 2026-11-26
- Bump: patch

## Added
- Preset `web-editor`: a game's executable built with the editor for the browser; edits change the running scene and are not kept across a reload. `tools/web/build_and_deploy.py --game <Game> --preset web-editor [--deploy --service <name>]` packages it as an editor page (`manifest.json` says so) and refuses to deploy it to a service without access control.
- `BagiDemo` (`tools/demo/`): the engine's demo scene of sprites, UI, particles, text and tweens for trying the editor, with the web target `engine-editor`.
- A browser game dials a local bridge (`tools/control/bagi_bridge.py`) and is driven by the control API through it; `tools/web/web_game.py` stands in for a browser game in `/qa`.
- The desktop editor attaches to a game in another process or a browser page: hierarchy, inspector, the remote picture in the Viewport, pick by click and Save over the control API (`scene_save`).

## Changed
- The SDL renderer and ImGui follow the display's pixel density (HiDPI), on the desktop too; every web page keeps a history of its log.
