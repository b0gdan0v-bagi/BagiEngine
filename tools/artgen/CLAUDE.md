# Art Generation Tools

## Overview

The image generator of the games' art is **ComfyUI**, today on the owner's Windows PC (RTX 3070, 8 GB VRAM). This folder installs it on any supported machine, starts it and checks it, and holds the service that turns a game's style preset and a subject into images. The editor and the agent (the skill `/artgen`: `.claude/skills/artgen/SKILL.md`) reach the generator only through that service, which talks to ComfyUI over HTTP and WebSocket. Nothing here is built or linked into the engine: ComfyUI (GPL-3.0) is a separate program, and its models stay on that PC, never in git. Python 3.10+, standard library only.

| File | Purpose |
|------|---------|
| `comfyui/manifest.json` | What is installed: the pinned ComfyUI, a variant per platform and every model and custom node with its pin, size, SHA256, licence and source. The one place to change a version or add a model |
| `artgen_setup.py` | The setup pipeline: `detect`, `install` (plan, consent, hash-checked downloads; idempotent), `start` / `stop`, `status` (`comfyui/CLAUDE.md`) |
| `comfyui/start-local.bat` | Starts a portable ComfyUI on `127.0.0.1:8188` by hand, in a window; the installer copies it into the root. Extra arguments go to ComfyUI (`--lowvram`) |
| `comfyui_check.py` | The health check: reachable, version, devices, every manifest model present; `--generate` makes one image; `--json` for the service and the editor panel |
| `artgen_bridge.py` | The link to the editor's Generate panel: a control API client that pulls requests (`artgen_poll`) and reports progress, images and the end |
| `artgen.py` | The service on the command line: `status`, `presets`, `generate` (`--json`: one progress event per line, then `done`; `--reference-image`, with `--reference-mode` and `--reference-weight`, the image alone meaning `init`) |
| `artgen_service.py` | `generate(request, generator, on_progress, cancel)`: N images of one preset and subject, each written with its provenance file as soon as it is there |
| `artgen_preset.py` | Presets and workflows: `parse_preset` (a file's content or a draft's), `find_presets`, `find_workflows`, `Workflow.describe` (the editor's form), `build_graph` (the graph of one image), `models_of` |
| `artgen_reference.py` | The reference picture of a run: `MODES` (`init`, `style`), `parse_reference`, `reference_of`, `effective_workflow` |
| `artgen_generators.py` | The generator interface (`status`, `info`, `run`, `choices`), `ComfyUIGenerator`, `FakeGenerator` (tests, `--generator fake`), `make_generator` |
| `presets/` | The engine's starter presets (`sprite`: one object cut out, `icon`, `sfx`: a sound effect), so a game without presets can generate at once |
| `artgen_ws.py` | A minimal WebSocket client for ComfyUI's progress stream; the frame codec is `tools/control/bagi_control_client.py`'s |
| `workflows/` | The engine's workflows: `<name>.json` is a graph in ComfyUI's API format (what `POST /prompt` takes), `<name>.inputs.json` names its inputs. `sdxl_basic` is the SDXL one; `sdxl_cutout` is the same with the background removed (an RGBA PNG); `sdxl_img2img` and `sdxl_img2img_cutout` are their variants that start from a reference picture (the map of `reference`, below); `sdxl_ipadapter` and `sdxl_ipadapter_cutout` start from noise and carry the picture's style through the IP-Adapter node (needs `install --style`; the picture goes in over flat grey, as in the img2img ones); `sao_sfx` makes a sound effect (Stable Audio Open, a FLAC) |
| `tests/` | pytest: a fake ComfyUI with a real WebSocket stream, the setup pipeline over fake platforms and a local download server, the fake generator, the presets, the manifest and workflow rules |

## Setup and models

`artgen_setup.py install` plans, asks and installs the pinned ComfyUI and the manifest's models (hash-checked). `comfyui_check.py` says what the running ComfyUI can make (`kinds`). The commands, platforms, consent, pin, model kinds, health check and the panel's setup actions are in `tools/artgen/comfyui/CLAUDE.md`.

## Presets

A game's style lives in its own repository: `<game root>/artgen/presets/<name>.json`. The engine's starters in `tools/artgen/presets/` come after them (a game's preset of the same name wins); the editor's form saves a starter as the game's own copy. A preset name is 1 to 64 of `A-Z a-z 0-9 _ -`.

```json
{"preset": 1, "title": "Carved wooden pieces", "workflow": "sdxl_basic",
 "prompt": "a carved wooden chess {subject} piece, studio lighting, plain background",
 "negative": "blurry, text", "size": [1024, 1024], "params": {"steps": 25, "cfg": 7.0}}
```

- `preset` is the format version; `workflow` and `prompt` are required. `{subject}` in the prompt is replaced by the request's subject (without it the subject goes first). `size` sets the workflow's `width` / `height`.
- `params` sets any other named input of the workflow (`steps`, `cfg`, `sampler`, `scheduler`, `checkpoint`, `vae` for `sdxl_basic`). A name the workflow does not have, or one the service sets (`prompt`, `negative`, `seed`, `width`, `height`), is refused with the file named, so a typo never silently changes nothing.
- The workflow is looked up in `<game root>/artgen/workflows/` first, then here: a game brings its own graph (a LoRA loader, img2img) with its own `.inputs.json`, and the preset stays node-id free. The `.inputs.json` must name `prompt`, `seed` and `output` (the node whose images are the result; its `filename_prefix` is set by the service).
- **`kind`** (`image` / `audio`) is the workflow's (`"kind"` of its `.inputs.json`, a reserved key like `output`); a preset may say it and must agree. A sound has no `size` but `params.duration` (seconds, a positive number); an image has no `duration`. One named input may set several nodes (`"duration": [["5", "seconds"], ["11", "seconds_total"]]`).
- **`accept`** (an image preset, optional): `{"trim": true, "padding": 2, "maxSize": 256}` is what the editor does to the picture at Accept (cut to what is drawn, a margin in percent, the longer side in pixels; a missing key keeps the default). The service ignores it: only the editor reads it (`src/Widgets/ArtGen/CLAUDE.md`, "Accept of an image").
- **`reference`** (an image preset, optional): `{"mode": "init", "image": "assets/artgen/rook.png", "weight": 0.4}` makes the run start from that picture instead of from noise; `mode` is a key of `MODES` in `artgen_reference.py`, `weight` is how strongly the picture holds (`0 < weight < 1`, default 0.4, so `init`'s `denoise` is `1 - weight`). A request's `reference` replaces the preset's whole, no key-by-key merge. A workflow's `.inputs.json` maps the modes it can run to the workflows that run them (`"reference": {"init": "sdxl_img2img"}`: a reserved key like `kind`); the preset names the plain workflow and the service picks the mapped one. A mapped workflow has the inputs `reference_image` (the `LoadImage`, set by the service) and the mode's weight input (`denoise`) and every named input of its base, so `params` work with or without a reference; `load_workflow` checks it. A workflow that has `reference_image` and no map serves its own mode, and is refused without a reference. `params` cannot hold the weight input when a reference runs. A picture with an alpha is laid on a flat light grey first, so a cut-out sprite's transparent pixels are not black.
- **A draft** is a preset's content sent inline (the editor's form, saved or not): `parse_preset` checks it with the same rules as a file, errors name `the draft of '<form name>'`, and its provenance says `preset: draft`.
- **The form is driven by the workflow.** `Workflow.describe(choices)` lists every named input but `output` with the graph's own value as `default`, and for a combo input the values the generator offers (`choices`: ComfyUI's `/object_info`, read once the generator is ready; `combo_choices` takes both its forms), so a new input of a workflow gets its field in the editor without code there.
- Models and LoRAs named by a preset must be on the generator's machine; the manifest holds the ones the engine knows, and a game's extra model is the game's to list with its licence.

## Generators

`status()` answers `ready`, `not_running` (installed or remote, not answering: offer **Start**) or `not_installed` (no install, or a manifest model missing: offer **Set up**, `artgen_setup.py install`), with a reason; the service refuses a request unless the state is `ready`. `ComfyUIGenerator` finds a local install at `artgen_setup.default_root()`; for another host it cannot know and says `not_running`. A run queues the graph with a `client_id`, follows its progress on `/ws` (falling back to polling `/history` when the stream fails), and fetches the `output` images by `/view`. `upload_image(data, name)` sends a reference picture to ComfyUI's input folder (`POST /upload/image`, `overwrite`) and returns the name to put into the graph; the service names the file by the hash of its bytes, so one picture is one file and a remote ComfyUI needs no shared folder. A cancel, a timeout or Ctrl+C deletes the prompt from the queue and interrupts it by its id, so the GPU is not kept busy and another client's run is left alone.

## The service

One prompt per image or sound, seeds `seed, seed + 1, ...` (random first seed when none is given), so every image can be made again alone from its provenance. `<stem>_<seed>.png` (a sound: `.flac`, the generator's own extension) and `<stem>_<seed>.artgen.json` go into the output folder (default `<game root>/artgen/out`; the editor's Accept moves the chosen one into the game's assets). The provenance holds the preset (its name or `draft`, its source `game` / `engine` / `draft`, its content as `presetData`) and its hash (of the file's bytes, of a draft's canonical JSON), subject, final prompt, negative, seed, size, params, the model files the graph loads, the workflow and its hash, the generator (name, version, address), its own file name and the date; its `kind`, and for a sound the asked `duration` and what the FLAC holds (`sampleRate`, `channels`, `seconds`). What is done stays on disk when a later image fails or is cancelled. A set's job goes into `<game root>/artgen/out/<set>/<job>/` beside the set's `manifest.json` (`src/Widgets/ArtGen/Sets/CLAUDE.md`).

**A reference** (the request's, else the preset's) is checked from files alone before the generator is asked anything, so a refusal writes nothing. `resolve_image` reads a relative `image` from the game root and refuses `..`, a backslash, a drive and a link out of the root (a control API request picks the path and the bytes go to another program); an absolute path is taken as it is, but not a UNC or device one (`//host/..`, `\?\`): Windows would connect to a host the client picked. The file is at most 16 MiB and a PNG, JPEG or WebP by its first bytes, not its name. The picture is uploaded once per request and the graph is the mapped workflow's; the provenance gets `reference` (`mode`, `image` as asked, `hash` of the file's bytes, `weight` as used, `workflow`) and the mapped workflow's `workflow` / `workflowHash` / `models`, so a variant can be made again alone.

## The editor bridge

```bash
python tools/artgen/artgen_bridge.py --port 8765        # the editor started with --control (--control-port); waits and reconnects
python tools/artgen/artgen_bridge.py --port 8765 --exit-with-editor --log FILE   # what the panel's Start the service runs (pythonw on Windows): ends with that editor, prints into FILE
```

The contract is the editor's (`src/Widgets/ArtGen/CLAUDE.md`, "The contract"): the bridge calls `artgen_poll` about twice a second with the generator's status, the presets (the game's and the engine's, with their source and content; a broken one with its error) and the workflows with their inputs (all refreshed every few seconds, at once when the answer says `refresh`: the editor saved a preset) of the game root the editor answered, runs a handed-over request (a preset by name, or a draft inline: `load_request_preset`) on a worker thread with `artgen_service.generate` into `<game root>/artgen/out`, and forwards its progress, each image and the end. A run that fails (any reason, a bug of ours too) ends with `artgen_done` `failed` and its reason; the bridge stays up. A cancel seen in a poll, a lost editor and Ctrl+C cancel the run on the generator. While the editor has no Generate panel (`unknown_method`: it started in Game mode) the bridge waits. A request may carry `outDir` (a `/`-separated path of 1 to 64 `A-Z a-z 0-9 _ -` segments, the folder of a set's job under `artgen/out`) and `name` (the file stem, the same rule); `resolve_out_dir` and `check_name` refuse anything else (`..`, an absolute path, a drive, a backslash, an empty segment) with a failed run before the generator is touched, and nothing is created. Both absent: `artgen/out` and the subject's slug. A `reference` (not an object, or a bad picture) fails the run with the reason the same way. The reported image paths stay real paths. A game ignores `artgen/out/` in its `.gitignore`.

## Running the tests

```bash
python -m pytest tools/artgen/tests        # .venv/bin/python on macOS
```
