# Art generation: the reference picture

## Overview

A run may start from a picture: `reference` is `{"mode": "init", "image": "<path>", "weight": 0.4}`. **What it means, which workflow runs it, how `image` is resolved and confined to the game root and what the provenance records is the service's** (`tools/artgen/CLAUDE.md`, "Presets" and "The service"); the editor only carries it and keeps it for the panel. Editor only, like the rest of `ArtGen/`.

| File | Purpose |
|------|---------|
| `ArtGenReference.{h,cpp}` | No ImGui: `ArtGenReference`, `ArtGenReferenceToJson`, `ReadArtGenReference` (the shape check), `kArtGenReferenceModes`, the builders `ArtGenReferenceOfFile`, `ArtGenReferenceOfVariant` (none for a sound) and `ArtGenReferenceOfSprite`, and the panel's helpers (`ArtGenReferenceFilePath`, `ArtGenReferenceFileName`, `ArtGenReferenceDefaultWeight`) |
| `ArtGenReferencePanel.{h,cpp}` | ImGui: the Reference section of the Generate tab over the session's slot |

## What the editor holds

- **The shape check is all C++ does.** An object, `mode` and `image` non-empty text, `weight` a number when present (null counts as absent); the error names the key (`reference.weight`). Whether the mode exists, the weight is in its range and the file is a picture is the service's: it fails the run with the reason, so a new mode is a row of `MODES` in `artgen_reference.py` and of `kArtGenReferenceModes`, nothing else here.
- **Where it goes.** `ArtGenSession::Request` / `RequestDraft` take it (`artgen_generate`'s `reference`; a bad shape is `invalid_params`), `ArtGenRun::reference` keeps it for the run, the poll's `request.reference` hands it over and `artgen_state.run.reference` echoes it (null when none). A new run clears it. A set's jobs never carry one; a preset's own `reference` reaches the service from the preset file and is replaced whole by a request's.
- **The slot** (`GetReferenceSlot` / `SetReferenceSlot` / `ClearReferenceSlot` of the session): the one picture the panel will send with the next single run. It is session state, so it survives a switch of the run mode and is shared by the Generate and the Set tab; it is sent only when the panel passes it to `Request`, never by the session itself. A change moves `Version()`; setting the same picture does not.
- **The builders** write the path the service reads back: `ArtGenReferenceOfFile` is relative to the game root with `/` when the file lies inside it (a moved game folder keeps it right), else the absolute path with `/`, and stays absolute when a `..` step is left in it; `ArtGenReferenceOfSprite` is `assets/artgen/<name>.png` (`ArtGenGameFile`), the file Accept wrote.
- **A preset's own `reference`** is an extra key of `ArtGenPresetForm`: loaded, kept by Revert and Duplicate, written after `accept`, and refused before a save when its shape is wrong (`CheckArtGenPresetShape`); the form never edits it, the slot is the way in. `ArtGenWorkflow::IsReferenceVariant` (the input `reference_image`, a service input) marks a workflow only a map of its base reaches, which the workflow combo may leave out.
- **The panel** shows the slot as a thumbnail (loaded by path, evicted when the slot changes or the panel dies; the strings are compared per frame, nothing is made), the mode combo, a weight slider (`kArtGenReferenceWeightMin` to `Max`; untouched it shows the mode's default and the slot keeps no weight, so the service's default applies) and Clear. `ArtGenReferenceDefaultWeight` repeats `default_weight` of `MODES`: a new mode adds its row there too. Generate passes the slot to `Request` for an image preset only; an empty slot sends nothing, so the preset's own `reference` applies.
