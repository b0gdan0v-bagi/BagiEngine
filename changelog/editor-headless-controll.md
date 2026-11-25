# Editor: an agent can run the game in editor mode without a window and take screenshots through the control API

- Version: 0.22.1
- Entry: editor-headless-controll
- Date: 2026-11-25
- Bump: patch

## Added
- The editor runs with `--headless` (`--editor --headless`): the editor and the game inside it draw into an offscreen SDL render target, and the control API's screenshots read it. Adapt: nothing.
- `status` of the control API has `runMode` (the mode that runs: the game or the editor). Adapt: nothing.
- The Python session (`GameSession`, the MCP launch tool) takes `editor=True` to start the editor; `stress_smoke.py` has an `editor` scenario. Adapt: nothing.
