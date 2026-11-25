# macOS: a --hidden-window run no longer takes the focus or switches Spaces

- Version: 0.25.3
- Entry: macos-hidden-window-steals-focus
- Date: 2026-11-25
- Bump: patch

## Changed
- With `--hidden-window` the SDL window sets `SDL_HINT_MAC_BACKGROUND_APP` before `SDL_Init`, so the process runs as a background app on macOS (no Dock icon, no activation): GPU smoke runs and benchmarks no longer pull the user out of a full-screen Space. Adapt: nothing.
