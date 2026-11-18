# A headless game has one window size: no phone, density, user scale or safe area over the control API

- Version: 0.1.12
- Entry: control-window-size
- Date: 2026-11-16
- Bump: patch

## Added
- The control method `set_window` (`{width, height, density?, userScale?, safeArea?}`) and `status.window`: a headless game lays out, renders and takes clicks in the simulated window; bounds are checked (`invalid_params`). Limits: `.claude/rules/control-window-api.md`.
- The start-up flags `--window=WxH` and `--density=D` for a headless run.
- `GameSession.set_window` in Python and the MCP tool `set_window`; a smoke step of the window.
