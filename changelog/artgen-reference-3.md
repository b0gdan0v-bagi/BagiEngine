# Art generation: a shape from a sketch as the reference (slice 3 of artgen-reference)

- Version: 0.4.5
- Entry: artgen-reference-3
- Date: 2026-11-19
- Bump: patch

## Added
- The reference mode `structure` ("this shape"): a SDXL ControlNet canny model (xinsir, Apache 2.0, pinned by sha256) of `kind` `structure` in the model manifest, installed by `artgen_setup.py install --structure`, the workflows `sdxl_controlnet` and `sdxl_controlnet_cutout` on the `reference` maps (ComfyUI's own ControlNet and Canny nodes), and the health check knows the part.
- The Generate panel takes a sketch file for the reference slot and offers **Use as shape** on a variant; the `artgen` skill and the docs describe the mode.
