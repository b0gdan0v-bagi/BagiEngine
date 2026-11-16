# Editor: presets cannot be made or changed in the Generate panel: a game without preset files generates nothing (workflow-driven form, draft, starter presets)

- Version: 0.1.10
- Entry: artgen-preset-editor
- Date: 2026-11-16
- Bump: patch

## Added
- The Generate panel edits presets in a form built from the workflow's inputs (defaults from the graph, combo values from the generator): New, Duplicate, Save, Save as, Revert; Generate works from the form as it is, saved or not (a draft).
- Engine starter presets `sprite` (cut-out on a transparent background) and `icon` in `tools/artgen/presets/`, listed after a game's own.
- Control method `artgen_save_preset {name, preset}`; `artgen_generate` takes a preset object (a draft) and `presetName`.
- `artgen_poll` carries each preset's `source` and content and the `workflows` with their inputs; its answer may say `refresh`.

## Changed
- The provenance of a generated image names its preset `draft` for an inline preset and records `presetSource` and `presetData`.
- `artgen_preset.find_presets` also returns the engine's starters; `load_preset` is `parse_preset` over a file. Adapt: a script that listed only the game's presets filters on `.source == "game"`.
