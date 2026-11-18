# Art generation: images have an opaque background (no sprite without a manual cut-out)

- Version: 0.1.19
- Entry: artgen-transparent-sprites
- Date: 2026-11-16
- Bump: patch

## Added
- Transparent sprites: the workflow `sdxl_cutout` (`tools/artgen/workflows/`) removes the background with ComfyUI's own `RemoveBackground` and BiRefNet and writes an RGBA PNG; a preset picks it with `"workflow": "sdxl_cutout"` (`tools/artgen/CLAUDE.md`, "Transparent sprites").
- The BiRefNet model (MIT) in `tools/artgen/comfyui/manifest.json`: `install.ps1` installs it into `models/background_removal/`, the health check reports it.

## Changed
- The Generate panel draws its thumbnails over a checkerboard, so a cut-out shows its transparency; Accept keeps the alpha.
