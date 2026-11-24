# Art Generation Panel

## Overview

The editor's **Generate** panel (`ArtGenWidget`, command `view.generate`, View → Generate Art): a style preset of the game and a subject become image variants, and a chosen one becomes a sprite of the game. The images come from the art generation service (`tools/artgen/`, its `CLAUDE.md`), a separate Python **client of the control API**; the editor knows neither ComfyUI nor any model. Editor only (`ArtGen` is an editor folder in `src/CMakeLists.txt`).

| File | Purpose |
|------|---------|
| `ArtGenSession.{h,cpp}` | The editor's side of the contract, no ImGui: the service's last status, one run, the save of a preset, the control methods. `Singleton`: it outlives the panel |
| `ArtGenSessionSets.cpp`, `ArtGenSessionControl.h` | The session's code for sets (`Sets/CLAUDE.md`); the error codes and param helpers of the two |
| `Reference/` | The reference picture of a run: `ArtGenReference`, its shape check, builders and slot (`Reference/CLAUDE.md`) |
| `Sets/` | Sets: a preset over a list of subjects, run as a queue of jobs; the model, the manifest on disk, the Set tab (`ArtGenSetPanel`) (`Sets/CLAUDE.md`) |
| `ArtGenPresetForm.{h,cpp}` | The preset form, no ImGui: the fields of one preset, its params as overrides of the workflow's defaults, dirty against what it was loaded with; `FormatArtGenPresetFile` (the file's key order), `CheckArtGenPresetShape` (what C++ checks before a save) |
| `ArtGenImage.{h,cpp}` | An image's processing at Accept, pure: `ArtGenImageSettings`, `DrawnBounds`, the frame (`MakeArtGenFrame`), `ProcessArtGenImage`, the PNG read and write |
| `ArtGenVerdict.{h,cpp}` | The verdict of the service's checks, pure: `ArtGenVerdictFromProvenance` and `ReadArtGenVerdict` (kind and tooltip text) from a `.artgen.json` |
| `ArtGenAtlasAccept.{h,cpp}` | Accept into an atlas group (`ArtGenImagePlan::atlas`), no CoreManager in its logic: `AcceptIntoGroup`, `PackGroupAfterAccept`, `PackAtlasForControl`; the rules are in its header |
| `ArtGenAccept.{h,cpp}` | Accept, no ImGui: an image or a sound processed and written into the game's assets, the library entry, the reload of what uses it, Use on a node (`AcceptForControl`, `AcceptSoundForControl`) |
| `ArtGenSound.{h,cpp}` | A sound's processing at Accept, pure: `ArtGenSoundSettings`, `ProcessArtGenSound`, their `Write...` / `Read...` JSON, `MeasureArtGenSound`, `BuildArtGenWaveform` |
| `ArtGenLaunch.{h,cpp}` | Start the service: finds the bridge of the engine checkout among the content roots and starts it |
| `ArtGenDrag.{h,cpp}` | The payload, rules and event of dragging a variant out: the contract of every panel that takes one |
| `ArtGenWidget.{h,cpp}` | The ImGui panel over the session: service line, setup, the preset form; under it the tabs Generate (the run, the grid of variants, Accept) and Set |

## The contract

The answers are the reflected structs of `ArtGenAnswers.h`, written by the generated `WriteJson`: an empty `Optional` writes no member, so "none" is an absent key, never `null`. By hand stay `artgen_poll` and `artgen_set` (`HandlePoll` / `HandleSet` answer the compact text of the reply, `Register` writes it with `Raw`: `request.preset` and the manifest's `presetData` are a draft's text in the middle of a document), the manifest and the provenance records and the preset and settings files (file formats).

**No DOM.** A handler takes a `JsonView`. A document the session or a set keeps (`presetData`, `ArtGenPreset::data`) is compact text with sorted keys (`SortedTextOf`; empty is none), taped where it is read; a workflow input's value is a typed `ArtGenInputValue` (the panel reads it every frame); a provenance record is a `JsonMemberMap`, so every key of the service's record is written back. Against the old DOM: of two equal keys the first wins, `inputs` are listed in key order, file readers skip a UTF-8 BOM first (`ArtGenFileText.h`), `1e999` is not a number.

The service **pulls**: a request made before the service connects is not lost, and the polls are how the editor knows the service is alive (`kServiceTimeoutSeconds` silent: gone). No push events (`Publish` is lost when nobody listens). Every method answers at once; a run is never a deferred call.

| Method | Caller | Params | Answer |
|--------|--------|--------|--------|
| `artgen_poll` | service, about every 0.5 s | `state` (`ready` / `not_running` / `not_installed`), `reason`, `generator`, `startedHere` (a generator a setup action started, so Stop can end it), `presets: [{name, title, source (game / engine), kind (image / audio), preset? (the content), error?}]`, `workflows: [{name, source, kind, inputs, error?}]` | `gameRoot` (the real path of the game, where `artgen/presets/` is), `refresh: true` once after a save, `request: {id, preset (a name, or the preset object of a draft), presetName? (with a draft), subject, count, seed\|null, outDir?, name? (a job of a set: its folder under `<gameRoot>/artgen/out` and file stem), reference?}` once, `cancel: id` while a cancel is asked, `setup: {id, action}` once, `setupCancel: id` while a setup cancel is asked, `shutdown: true` once after Disconnect (the service exits) |
| `artgen_setup_report` | service, per event of a setup action | `id, event`: `plan` (`platform, note, root, downloadBytes, steps: [{what, dest, state, size, licence}]`), `step`, `progress` (`done, total`), `log`, `done` (`ok`, `error`) | `{stale}` |
| `artgen_progress` | service | `id, image, count, step, steps, seed` | `{stale}` |
| `artgen_image` | service, per image or sound | `id, image, count, seed, path, provenance` (real paths of the file and its `.artgen.json`), `kind` (`image` when absent, `audio`) | `{stale}` |
| `artgen_done` | service, once per run | `id, status` (`ok` / `failed` / `cancelled`), `error` | `{stale}` |
| `artgen_generate` | an agent, a test | `preset` (a name, or a preset object: a draft), `presetName?`, `subject, count?` (1..16, default 4), `seed?`, `reference?` (`{mode, image, weight?}`, `Reference/CLAUDE.md`) | `{id, serviceConnected}`; `artgen_busy` while a run is in work, `invalid_params` (also a draft whose shape is not a preset) |
| `artgen_save_preset` | an agent, a test | `name, preset` | `{path}` of `<gameRoot>/artgen/presets/<name>.json`; `invalid_params` (a bad name or shape, no game root, a failed write) |
| `artgen_cancel` | an agent, a test | none (the run in work; of a set, its running job only), or `job` (+ `set`, needed when two sets have a job of that name in work), or `set` + `all: true` | `{}`; with `job` or `set` `{set, job?, state}`; `artgen_no_run`, `artgen_no_set`, `invalid_params` |
| `artgen_enqueue` | an agent, a test | `preset` (a name or a draft object), `presetName?`, `subjects` (a string of lines, or a list of strings and `{name?, subject}`), `count?`, `seed?`, `set?`, `frameOf?` (an image set that has a frame: the new set is cut by it), `reference?` (as `artgen_generate`'s; every job runs with it), `atlas?` (the group an image set's sprites join; default the preset's `atlas`, `""` none) | `{set, jobs: [names], seed, serviceConnected}`; `invalid_params` (also a set whose folder exists, a bad `frameOf`) |
| `artgen_set` | an agent, a test | `set` | the manifest of the set plus `state` (`Active`, `Complete`); `artgen_no_set` |
| `artgen_pick` | an agent, a test | `set, job, seed` | `{set, job, pick}`; on a live row it writes the variant into the game and adds `switched` (the acceptor's answer; `Sets/CLAUDE.md`, "Accept all"); `artgen_no_set`, `artgen_no_variant` (also any pick but its own on a kept row), `artgen_accept_failed` (a switch that wrote nothing, or a replaced row: names that set) |
| `artgen_retry` | an agent, a test | `set, job?` | `{set, retried}`; `artgen_no_set` |
| `artgen_more` | an agent, a test | `set` (not active), `count?` (1..16, default 1), `seed?`, `jobs?` (names) | `{set, seed, count, jobs: [queued], skipped: [{job, reason}]}` (with a preset's `regenerate`, up to `count + extra`; `Sets/CLAUDE.md`, "The queue"); `reason`: `kept`, `failed`, `cancelled`, `full`; `artgen_no_set` (also an unknown job), `artgen_busy`, `invalid_params` (also a seed that meets a variant, or nothing to run) |
| `artgen_accept` | an agent, a test | `seed` (a variant of the current run), `useOnSelected?`; an image: `sprite`, `image?` (`WriteArtGenImageSettings`'s object, a missing key keeps the preset's), `atlas?` (a group name; default the preset's `atlas`, `""` none); a sound: `name` or `clip`, `sound?` (`WriteArtGenSoundSettings`'s object, a missing key keeps the default) | an image: `{sprite, texturePath, png, width, height (the written file's), processing, reloadedSprites, atlasGroup (the group the sprite joined), atlas ({name, texturePath, width, height, members} when the group was packed), usedOn?}`; a sound: `{clip, wav, sampleRate, channels, seconds, peakDb, reloadedSources, usedOn?}`; `atlasGroup`, `atlas` and `usedOn` are absent when none; `artgen_no_variant`, `invalid_params` (bad sound settings), `artgen_accept_failed` (a bad name or clip path, no library file, no selected node or component, a sound that trims to nothing) |
| `artgen_setup` | an agent, a test | `action`: `Plan`, `Install`, `Start`, `Stop`, or `cancel` | `{id, serviceConnected}`; `artgen_busy` while a setup or a run is in work, `artgen_no_setup` (cancel with none), `invalid_params` |
| `artgen_accept_set` | an agent, a test | `set` (every job done; a sound also picked) | `{set, accepted: [{job, as, seed, alternatives, ...the acceptor's answer}], atlas}` (`atlas`: the one pack of the set's group) (a kept row: `{job, kept: true}`, a row another set accepted later: `{job, replacedBy}`; `Sets/CLAUDE.md`, "Accept all"); `artgen_no_set`, `artgen_busy` (the set is active), `artgen_set_incomplete` (names the jobs not picked, failed or cancelled), `artgen_accept_failed` (a row's file gone or a row failed: names it and the rows done, or the library save of an image set or the pack of its group failing: the first marks no row, the second leaves them accepted) |
| `artgen_keep` | an agent, a test | `set, job?` | `{set, kept: [jobs], replaced?: [{job, by}]}` (`Sets/CLAUDE.md`, "Keep"); `artgen_no_set`, `artgen_busy` (jobs to run), `invalid_params` (nothing live, or the row is replaced by a newer set), `artgen_accept_failed` |
| `artgen_state` | an agent, a test | none | `{service: {connected, polls, state, reason, generator, presets, workflows}, run: {id, state, preset, draft, subject, count, seed?, reference?, image, step, steps, error, cancelRequested, variants: [{seed, path, provenance, kind}]}, setup: {id, action, state, plan, current, done, total, log, error, ...}, sets: [{name, preset, kind, count, seed, state, jobs: {pending, running, done, failed, cancelled}, picked, accepted, live, kept, reference?, atlas?}], gameRoot}`; `seed`, `reference` and `atlas` are absent when none |

- **One run at a time.** A request is `Queued` until a poll hands it over (`Running`), then `Done`, `Failed` or `Cancelled` by `artgen_done`. A report whose `id` is not the running one is answered `{"stale": true}` and changes nothing.
- **Cancel.** A queued run is cancelled at once; a running one when the service, after it saw `cancel` in a poll, answers `artgen_done` `cancelled`.
- **Setup actions** (`ArtGenSetup`) travel the same way and never beside a run (`RequestSetup` and `Request` refuse each other's time). Install is offered only under the plan: the service downloads without asking.
- The panel and `artgen_generate` go through the same `ArtGenSession::Request` (by name) or `RequestDraft` (inline). A preset the service reported broken is refused by name; a draft is checked here only for its shape, the rest by the service, which fails the run with the reason.
- **Save** (`SavePreset`, `artgen_save_preset`): the C++ side checks only the name and the shape; the file is written atomically in the docs' key order, shown at once as the game's, and the next poll asks the service to `refresh`, whose verdict comes back as the preset's `error`. `keepVariants` starts the new run with the variants of the last one (not of a set's job: they are its row's).

## Lifetime and threads

The control API cannot drop a handler and the panel is re-created at every switch of the run mode, so the session registers the methods (`RegisterControl`, once per `ControlService`, from the panel's `Initialize`) and they capture it, never the widget. Before the panel exists the service gets `unknown_method`. Main thread only.

## The panel

- **Tabs.** Above them, shared: the service line, setup and the preset form. Each tab owns its controls: **Generate** the subject, count, seed, keep-the-variants, Generate / Cancel and the run view; **Set** (`ArtGenSetPanel`) the queue and the sets (`Sets/CLAUDE.md`, "The panel"). Generate is off while a set runs.
- **The preset form** (`ArtGenPresetForm`): the combo lists the game's presets, then the engine's starters, a broken one with its error; picking one drops unsaved edits (marked `*`). Save makes a starter the game's copy. Fields: Name, Title, Workflow, Prompt, Negative, the workflow's inputs from the service's description, and, for an image workflow, "Accept (image)" and "Reference" (`Reference/CLAUDE.md`). Generate sends a saved, unchanged preset by name and anything else as a draft.
- **Start the service** first turns the control API on when it is off (`ApplicationEvents::StartControlServerRequestEvent`), then, once it listens, runs the bridge of the engine checkout in the background (`ArtGenLaunch`); its log is `artgen-bridge.log` in the editor's user folder.
- **Set up** (`not_installed`) shows the service's plan; **Download and install** runs it with a progress bar and Cancel; Not now forgets it; beside it a line says a generator on another machine is not there yet (entry `comfyui-remote-via-todo`, `tools/artgen/comfyui/CLAUDE.md`). **Start** (`not_running`) starts the local generator; **Stop ComfyUI** is there only for a generator started from the panel (`startedHere`). **Disconnect** stops the service itself (`RequestServiceStop`).
- Thumbnails are loaded from the reported paths and **evicted** when the run is replaced or the panel dies.
- **Sounds.** A run of sound variants (`kind` audio) is a list instead of the grid: Play / Stop of each (a FLAC as an `AudioClip` on the `Ui` bus, evicted with the run), its waveform, seed, length and peak; a click on the waveform picks it. Under the pick: the processing settings, **Listen as accepted**, the target (a name, or **Replace a clip of the game**) and Accept.
- A thumbnail is drawn over a checkerboard, so a cut-out shows its transparency. **The verdict mark:** every image thumbnail of the grid and of the Set tab's rows has a mark in its top-right corner from `checks` of its provenance file, read once when the thumbnail loads (`ArtGenVerdict`): passed, failed (the tooltip names the checks), unreadable (`passed: null`); a sound or a file without `checks` has none.
- **Reference** (`Reference/`, Generate tab, not for a sound preset): the slot's picture, mode, weight and Clear; **Use as reference** on a variant or a sprite, **Use file** and **Use as shape** fill it. The workflow combo leaves out the variants a map reaches.
- **Drag out.** An image variant of the grid and of a Set row is a drag source (a click still selects). The payload names the variant, never its file. A target plans with `PlanArtGenDrop` and emits `ArtGenVariantDropEvent`; this panel answers with the form's accept settings (a set's variant: `AcceptSetVariant`, the manifest untouched) and the Accept message (`TextureEditor/CLAUDE.md`, "Drop of a generated variant").
- A click selects a variant; the dice button under it puts its seed into the form for the next Generate.

## Accept

`ArtGenSession::Accept(seed, sprite, useOnSelected, settings)` (the panel's button, with the form's accept settings, and `artgen_accept`) calls the installed acceptor, `AcceptForControl`; the session needs no file system.

- **Files.** `ArtGenGameFile` is the one path builder the panel's text uses. The processed PNG (next section) and its `.artgen.json` (the generation's record plus `accepted`) go to `<game assets>/artgen/<sprite>.png` and `.artgen.json`, replacing an earlier accept of the name. After Keep that file also holds `"kept": {seed, date}`; a later write of the name replaces the file whole, so only a kept row has it. A sprite name: `IsValidSpriteName`.
- **Library.** The entry `{sprite, assets/artgen/<sprite>.png, the written size}` is added or replaced and the game's `TextureLibrary.xml` saved. An Accept changes only the `<sprite>` lines of the file (`Resource/CLAUDE.md`). A sprite in an **atlas group keeps its link** (`AcceptedSpriteEntry`); a **packed** one keeps its place in the atlas and only its `atlas.source` becomes the new PNG, then `AcceptIntoLibrary` repacks the group. A group named by the plan (`atlas`: the preset's, the call's or the set's) makes the sprite a member and packs the group; a packed member of another group is refused (`ArtGenAtlasAccept.h`). A set saves the library once, inside one `ArtGenLibraryBatch` (`Sets/CLAUDE.md`, "Accept all"). Every step saves through `SaveGameLibraryForAccept`: with a batch open nothing writes `TextureLibrary.xml` before `EndAcceptBatch`.
- **Reload.** The PNG path and, after a repack, the atlas path are evicted from the resource cache; every `SpriteRendererComponent` of the active scene that names the sprite or the PNG path is reloaded, and so is every one that names another member of the repacked atlas. A canvas image, a `ParticleEmitterComponent` and the Sprite Library notice the evict themselves (`ResourceCache::Holds`); RmlUi's `<img>` and another scene's sprites do not follow.
- **Use on selected.** The session keeps the scene editor's primary selection (`SceneSelectionChangedEvent`; found alive in the active scene before use). The sprite goes into its `SpriteRendererComponent::_spriteId` (a node without one is refused), and `SceneEditedEvent` marks the scene unsaved.

## Accept of an image

`ArtGenImageSettings {trim, padding, maxSize}`: the form's (panel), else the call's `image` over the run's preset (a draft: own `accept` or the defaults; by name: the saved preset's). The processing is pure (`ArtGenImage`: `DrawnBounds`, `MakeArtGenFrame`, `ProcessArtGenImage`; their comments have the rules). A single Accept: a frame of its own. An image with nothing drawn is refused.

- **Identity** (`trim` off, `maxSize` 0): PNG and provenance are copied byte for byte, no `accepted`.
- **A set keeps one frame** (the rule and the legacy case: `Sets/CLAUDE.md`, "Accept all"); a variant that does not fit it fails per row in the acceptor. The measure over all variants is the session's `FrameMeasurer` (`MeasureFrameForControl`); with none installed an Accept all that is not identity is refused.

## Accept of a sound

`ArtGenSession::AcceptSound(seed, target, settings, useOnSelected)` (the panel and `artgen_accept` of a sound) calls `AcceptSoundForControl`.

- **Processing:** `ProcessArtGenSound`; its comment has the order.
- **Files.** A 16-bit WAV (`EncodeWav`) at `assets/artgen/<name>.wav` or over the game's clip at `clip` (`IsValidClipPath`: a game plays its sounds by fixed paths), in the top `assets` layer, written to a temporary file and renamed over the old one; beside it `<file>.artgen.json`: the provenance plus `accepted` (clip, source FLAC, settings, what was written).
- **Reload.** The clip's path is evicted from the resource cache and every `AudioSourceComponent` of the active scene that names it calls `ReloadClip`. Use on selected sets the clip of the node's `AudioSourceComponent` (a node without one is refused before anything is written).

## Tests

`tests/BECore/ArtGen*Test.cpp` call the methods as the service does, with a fake acceptor and a temporary game root; the service side is `tools/artgen/tests/test_artgen_bridge.py`.
