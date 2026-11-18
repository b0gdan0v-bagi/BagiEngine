# ComfyUI manifest

`manifest.json` pins the ComfyUI version and every model the engine's workflows load; `artgen_setup.py` installs from it and `comfyui_check.py` checks it (`tools/artgen/CLAUDE.md`, "Setup"). This file holds the rules for what may enter it.

## Models

Only models whose licence lets the generated assets ship in a commercial game, each with its licence and source in the manifest. A fine-tune or a LoRA is added only after its licence was read; Flux and other models above 8 GB are out for now. A Hugging Face URL names a commit, not `main` (a test holds it), so the pinned hash stays true.

**Sounds.** A model has a `kind` (`image` by default, `audio`). The audio ones, Stable Audio Open 1.0 (Stability AI Community License, commercial use free under USD 1M of yearly revenue; `THIRD_PARTY_NOTICES.md`) and its T5-base text encoder, are installed only by `install --audio` (5.7 GB). The checkpoint is **gated**: accept its licence on Hugging Face, then give the install a token in `HF_TOKEN` or `hfToken` of `local.json` (never in git; it goes to huggingface.co only). Hugging Face hides a gated file's hash without access, so a gated model may enter the manifest with `sha256: null` (SAO's is pinned): the install reads the hash at the pinned commit with the token (`gated_sha256`) and prints it to be pinned; a file placed by hand without a token is checked by its size. ComfyUI's own Stable Audio nodes run it (`CLIPLoader` type `stable_audio`, `SaveAudio`: a FLAC), no custom node.
