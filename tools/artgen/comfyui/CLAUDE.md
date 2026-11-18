# ComfyUI manifest

`manifest.json` pins the ComfyUI version and every model the engine's workflows load; `artgen_setup.py` installs from it and `comfyui_check.py` checks it (`tools/artgen/CLAUDE.md`, "Setup"). This file holds the rules for what may enter it and the rules of the install.

## Models

Only models whose licence lets the generated assets ship in a commercial game, each with its licence and source in the manifest. A fine-tune or a LoRA is added only after its licence was read; Flux and other models above 8 GB are out for now. A Hugging Face URL names a commit, not `main` (a test holds it), so the pinned hash stays true.

**Sounds.** A model has a `kind` (`image` by default, `audio`). The audio ones, Stable Audio Open 1.0 (Stability AI Community License, commercial use free under USD 1M of yearly revenue; `THIRD_PARTY_NOTICES.md`) and its T5-base text encoder, are installed only by `install --audio` (5.7 GB). The checkpoint is **gated**: accept its licence on Hugging Face, then give the install a token in `HF_TOKEN` or `hfToken` of `local.json` (never in git; it goes to huggingface.co only). Hugging Face hides a gated file's hash without access, so a gated model may enter the manifest with `sha256: null` (SAO's is pinned): the install reads the hash at the pinned commit with the token (`gated_sha256`) and prints it to be pinned; a file placed by hand without a token is checked by its size. ComfyUI's own Stable Audio nodes run it (`CLIPLoader` type `stable_audio`, `SaveAudio`: a FLAC), no custom node.

## The install

- **Consent.** `install` prints every download (size, destination, licence) and asks before the first byte; `--yes` is for a caller that asked (the panel shows the same plan), `--json` prints the plan and one event per line (`plan`, `step`, `progress`, `step_done`, `log`, `done`) and needs `--yes`. A run that only checks hashes asks nothing.
- **The version is pinned.** Never run the `update\*.bat` of the portable folder: it moves ComfyUI off the manifest. To upgrade, change `comfyui` in the manifest (version, the tag's commit, per portable variant the URL, size and the `sha256` of the release API's `digest`) and install into a new root.
- The installer never overwrites or deletes what it did not download: another ComfyUI version, a non-empty root or a model file with another hash stops it with what to do. A broken download stays as `<file>.part` and the next run resumes it.
