# Art generation: no reference image (no variations of a picture, no style of an accepted sprite, no shape from a sketch)

- Version: 0.3.5
- Entry: artgen-reference
- Date: 2026-11-18
- Bump: patch

## Added
- Art generation takes a reference picture in the `init` mode (image to image): `reference: {mode, image, weight}` in a preset, a request, the control API's `artgen_generate` and the command line of `tools/artgen/artgen.py`. The picture is uploaded to ComfyUI and recorded in the provenance of each result.
- The workflows `sdxl_img2img` and `sdxl_img2img_cutout` in `tools/artgen/workflows/`; a workflow's `.inputs.json` names where the reference goes through its `reference` map.
- The editor's Generate panel has a reference slot, and a variant of a set can be sent to it with **Use as reference**.
- The `artgen` skill describes how to make variations of a picture.
