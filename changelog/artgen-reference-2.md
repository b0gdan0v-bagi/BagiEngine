# Art generation: a sprite's style as the reference (slice 2 of artgen-reference)

- Version: 0.4.3
- Entry: artgen-reference-2
- Date: 2026-11-19
- Bump: patch

## Added
- The reference mode `style` ("in the style of this sprite"): IP-Adapter for SDXL and its CLIP vision model in the model manifest, the custom node installed by `artgen_setup.py install --style`, the workflows `sdxl_ipadapter` and `sdxl_ipadapter_cutout` on the `reference` maps; the health check knows the style parts and the service refuses a style run when they are missing.
- A set can carry a reference: the set manifest key, `artgen_enqueue` and the sets state hold it, the Set tab sends the slot with Queue and shows the set's reference, and the `artgen` skill styles a set by its first accepted sprite.
- The preset form edits a preset's own `reference` (a Reference subsection).
- A style run gets the cut-out checks, `minPass` and `extra` like any other run, and its thumbnails show the verdict mark.

## Changed
- The setup, the models, the health check and the setup actions of the art tools are documented in `tools/artgen/comfyui/CLAUDE.md`.
