# Perf: the 2D benchmarks run on the SDL hardware renderer and on Vulkan with a hidden window

- Version: 0.23.1
- Entry: stress-sprites-gpu
- Date: 2026-11-25
- Bump: patch

## Added
- Engine start flags `--backend=software|sdl-gpu|vulkan` and `--hidden-window`: one run on the chosen renderer with a borderless unfocusable window off the screens, vsync off, a fixed 1920 x 1080 output; `software` needs `--headless`, the GPU backends refuse it; an unavailable backend ends the run with a marker the tools read as a skip. Adapt: nothing.
- BagiStress `--stress-no-batching` and a `batching` key in its results; the engine's perf set has `stress-sprites-gpu` and `stress-sprites-vulkan` (skipped where the backend is missing). Adapt: nothing.
