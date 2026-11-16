# Art generation: sounds (Stable Audio Open through ComfyUI's own nodes)

- Version: 0.1.11
- Entry: artgen-audio-kind
- Date: 2026-11-16
- Bump: patch

## Added
- Presets and workflows have a `kind` (`image` by default, `audio`); an audio preset takes `params.duration` (seconds) instead of `size`.
- Workflow `sao_sfx` (Stable Audio Open 1.0 on ComfyUI's own nodes, a FLAC) and the engine starter preset `sfx` on it.
- The manifest lists Stable Audio Open 1.0 (gated, Stability AI Community License) and its T5-base encoder; `artgen_setup.py install --audio` installs them with a Hugging Face token from `HF_TOKEN` or `hfToken` of `tools/artgen/local.json`.
- `comfyui_check.py --generate audio`; the report lists what the generator makes (`kinds`).
- The provenance of a sound records `kind`, `duration`, `sampleRate`, `channels` and `seconds`; `artgen_image` and the presets of `artgen_poll` carry `kind`.

## Changed
- The service refuses a request of a kind the generator cannot make; a variant's file suffix comes from the generator's file (`.flac` for a sound). Adapt: a script that assumed `.png` reads the path of the event.
