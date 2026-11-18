# Art Generation Tools

## Overview

The image generator of the games' art is **ComfyUI**, today on the owner's Windows PC (RTX 3070, 8 GB VRAM). This folder installs, starts and checks it on any supported machine and holds the service that turns a game's style preset and a subject into images. The editor and the agent (skill `/artgen`) reach the generator only through that service, over HTTP and WebSocket. Nothing here is linked into the engine: ComfyUI (GPL-3.0) is a separate program, and its models stay on that PC, never in git. Python 3.10+, standard library only.

| File | Purpose |
|------|---------|
| `comfyui/manifest.json` | What is installed: the pinned ComfyUI version and commit, a variant per platform (a portable release asset with URL, size, SHA256; or a source install with its PyTorch index) and every model (folder, pinned Hugging Face commit URL, size, SHA256, licence, source). Where a version or a model is changed |
| `artgen_setup.py` | The setup pipeline: `detect` (the platform key), `install` (plan, consent, downloads checked against the manifest; idempotent), `start` / `stop` (a background ComfyUI on the address of `local.json`), `status` |
| `comfyui/start-local.bat` | Starts a portable ComfyUI on `127.0.0.1:8188` by hand, in a window (the installer copies it into the root); extra arguments go to ComfyUI |
| `comfyui/CLAUDE.md` | What may enter the manifest: licences, pinned commits, the gated audio models |
| `comfyui_check.py` | The health check: reachable, version, devices, every manifest model present; `--generate` makes one image; `--json` for the service and the editor panel |
| `artgen_bridge.py` | The link to the editor's Generate panel: a control API client that pulls requests (`artgen_poll`) and reports progress, images and the end |
| `artgen.py` | The service on the command line: `status`, `presets`, `generate` (`--json` prints one progress event per line, then `done`; `--min-pass` / `--extra`; `--reference-image` with `--reference-mode` / `--reference-weight`, the image alone meaning `init`), `check <png>... [--preset NAME]` (the verdict of files on disk; exit 1 when one fails or cannot be read, 2 for a file or preset that cannot be opened) |
| `artgen_service.py` | `generate(request, generator, on_progress, cancel)`: N images of one preset and subject, each written with its provenance as soon as it is there |
| `artgen_preset.py` | Presets and workflows: load and check them (`parse_preset` on a file's content or a draft's), find them in the game and the engine (`find_presets`, `find_workflows`), `Workflow.describe` (the inputs and their defaults for the editor's form), `build_graph` (the graph of one image), `models_of` |
| `artgen_reference.py` | The reference picture of a run: `MODES` (`init` now), `parse_reference`, `reference_of`, `effective_workflow` |
| `artgen_alpha.py`, `artgen_checks.py` | The alpha of a PNG without PIL (`read_alpha`: `None` when it has none) and the checks of a cut-out over it (`DEFAULTS`: every threshold and what it means; `check_png`: the verdict) |
| `artgen_generators.py` | The generator interface (`status`, `info`, `run`, `choices`: the values of its combo inputs), `ComfyUIGenerator`, `FakeGenerator` (tests, `--generator fake`), `make_generator` |
| `presets/` | The engine's starter presets (`sprite`: one object cut out on a transparent background, `icon`: a flat icon; `sfx`: a short sound effect on `sao_sfx`), so a game without presets can generate at once |
| `artgen_ws.py` | A minimal WebSocket client for ComfyUI's progress stream; the frame codec is `tools/control/bagi_control_client.py`'s |
| `workflows/` | The engine's workflows: `<name>.json` is a graph in ComfyUI's API format (what `POST /prompt` takes; the UI loads it by drag and drop), `<name>.inputs.json` names its inputs. `sdxl_basic` (SDXL), `sdxl_cutout` (the same with the background removed: an RGBA PNG), `sdxl_img2img` and `sdxl_img2img_cutout` (their variants that start from a reference picture: the `reference` map, below), `sao_sfx` (a sound effect, Stable Audio Open: a FLAC) |
| `tests/` | pytest over fakes (ComfyUI with a real WebSocket stream, platforms, a download server) |

## Setup

```bash
python tools/artgen/artgen_setup.py install --dry-run    # the platform, the downloads (9 GB at first)
python tools/artgen/artgen_setup.py install              # asks, then installs; a second run only checks the hashes
python tools/artgen/artgen_setup.py start                # in the background; `stop` ends it
python tools/artgen/comfyui_check.py --generate          # exit 0: ready
```

- **One pipeline, a variant per platform.** `detect` reads the OS, the CPU and the GPU names and picks a key of `comfyui.platforms`: `windows-nvidia` (also `-arm64`), `windows-amd`, `windows-intel` take the portable release asset of the pinned version; `macos-arm64` and `linux-nvidia` (the CUDA index) clone the pinned tag, check it is the manifest's commit, and build `venv/` with PyTorch and ComfyUI's `requirements.txt` (PyPI versions are not pinned). A discrete NVIDIA or AMD GPU wins over Intel graphics; an Intel Mac, Linux without NVIDIA and a machine without a GPU have no variant. `--platform` overrides it.
- **Consent.** `install` prints every download (size, destination, licence) and asks before the first byte; `--yes` is for a caller that asked (the panel shows the same plan), `--json` prints the plan and one event per line (`plan`, `step`, `progress`, `step_done`, `log`, `done`) and needs `--yes`. A run that only checks hashes asks nothing.
- **The root** is `--root`, else `comfyuiRoot` of `local.json`, else `D:\ComfyUI` on a Windows PC with a D: drive, else `~/ComfyUI`; a finished install writes it into `local.json`. Both layouts keep ComfyUI in `<root>/ComfyUI` (models in `models/<folder>/`); `<root>/.artgen-setup.json` records the platform.
- **Start and stop.** `start` runs the portable's embedded Python or the venv's on the host and port of the address (`resolve_url`), logs to `<root>/artgen-comfyui.log`, keeps the pid in `artgen-comfyui.pid` there and waits until it answers. Arguments after `--` go to ComfyUI (`--lowvram`; a second instance beside a running one needs `--disable-assets`: they share a database). `stop` ends only the process it started, after checking that pid still is that ComfyUI; one started by hand is closed by hand.
- **Local only, on demand.** ComfyUI listens on `127.0.0.1`: no LAN port, no firewall rule (cancel Windows' prompt; the PC is on a foreign network). The Mac reaches it through the todo service and this machine's worker agent (entry `comfyui-remote-via-todo`), never by a port. No service, no scheduled task: the GPU is free while ComfyUI is closed (the check says `unreachable`).
- **The version is pinned.** Never run the `update\*.bat` of the portable folder: it moves ComfyUI off the manifest. To upgrade, change `comfyui` in the manifest (version, the tag's commit, per portable variant the URL, size and the `sha256` of the release API's `digest`) and install into a new root.
- The installer never overwrites or deletes what it did not download: another ComfyUI version, a non-empty root or a model file with another hash stops it with what to do. A broken download stays as `<file>.part` and the next run resumes it.

## Models

The rules for what enters the manifest (licences, the pinned commits, the gated audio models) are in `comfyui/CLAUDE.md`.

**Transparent sprites.** ComfyUI removes backgrounds with BiRefNet (`LoadBackgroundRemovalModel`, `RemoveBackground`; `birefnet.safetensors`, MIT, in the manifest). `sdxl_cutout` runs it after the VAE decode (`RemoveBackground` gives 1 on the subject, `JoinImageWithAlpha` takes 1 as transparent, so an `InvertMask` sits between) and `SaveImage` writes an RGBA PNG. A preset picks it with `"workflow": "sdxl_cutout"`; a prompt of one subject on a plain background cuts best. Its named input `bg_model` swaps the removal model.

## The health check

`comfyui_check.py` reads the address from `--url`, else `COMFYUI_URL`, else `comfyuiUrl` of the git-ignored `tools/artgen/local.json`, else `http://127.0.0.1:8188`. Exit 0 ready, 1 reachable but not ready (an image model missing, the generation failed), 2 unreachable. A missing audio model is a warning; `kinds` says what it can make (the service refuses a sound without it). `--generate audio` makes one sound with `sao_sfx`. Another ComfyUI version or only a CPU is a warning. `--generate` puts a fresh seed into every sampler: ComfyUI caches node results, and a repeated graph would return without touching the GPU.

## Presets

A game's style lives in its own repository: `<game root>/artgen/presets/<name>.json`. The engine's starters in `tools/artgen/presets/` come after them (a game's preset of the same name wins); the editor's form saves a starter as the game's copy. A preset name is 1 to 64 of `A-Z a-z 0-9 _ -`.

```json
{"preset": 1, "title": "Carved wooden pieces", "workflow": "sdxl_basic",
 "prompt": "a carved wooden chess {subject} piece, studio lighting, plain background",
 "negative": "blurry, text", "size": [1024, 1024], "params": {"steps": 25, "cfg": 7.0}}
```

- `preset` is the format version; `workflow` and `prompt` are required. `{subject}` in the prompt is replaced by the request's subject (without it the subject goes first). `size` sets the workflow's `width` / `height`.
- `params` sets any other named input of the workflow (`steps`, `cfg`, `sampler`, `scheduler`, `checkpoint`, `vae` for `sdxl_basic`). A name the workflow does not have, or one the service sets (`prompt`, `negative`, `seed`, `width`, `height`), is refused with the file named, so a typo never passes silently.
- The workflow is looked up in `<game root>/artgen/workflows/` first, then here: a game brings its own graph (a LoRA loader, img2img) with its `.inputs.json`, and the preset stays node-id free. The `.inputs.json` must name `prompt`, `seed` and `output` (the node whose images are the result; the service sets its `filename_prefix`).
- **`kind`** (`image` / `audio`) is the workflow's (`"kind"` of its `.inputs.json`, a reserved key like `output`); a preset may say it and must agree. A sound has no `size` but `params.duration` (seconds, a positive number); an image has no `duration`. One named input may set several nodes (`"duration": [["5", "seconds"], ["11", "seconds_total"]]`).
- **`accept`** (an image preset, optional): `{"trim": true, "padding": 2, "maxSize": 256}` is what the editor does to the picture at Accept (cut to what is drawn, a margin in percent, the longer side in pixels; a missing key keeps the default). Only the editor reads it (`src/Widgets/ArtGen/CLAUDE.md`, "Accept of an image").
- **`checks`** (an image preset, optional; a sound has none): `false` switches the checks of a cut-out off, an object sets thresholds by the names of `artgen_checks.DEFAULTS` (a missing name keeps its default; an unknown name or a bad value is refused with the file named). Each verdict names the thresholds used, `presetData` the file's own.
- **`regenerate`** (an image preset, optional): `{"extra": 6, "minPass": 3}` lets the service make up to `extra` (1 to 16) more images when fewer than `minPass` (default: the `count`) pass their checks. A request's keys replace it (`extra` 0 switches it off); refused for a sound and with `checks: false`.
- **`reference`** (an image preset, optional): `{"mode": "init", "image": "assets/artgen/rook.png", "weight": 0.4}` makes the run start from that picture instead of from noise; `mode` is a key of `MODES` in `artgen_reference.py`, `weight` is how strongly the picture holds (`0 < weight < 1`, default 0.4, so `init`'s `denoise` is `1 - weight`). A request's `reference` replaces the preset's whole, no key-by-key merge. A workflow's `.inputs.json` maps the modes it can run to the workflows that run them (`"reference": {"init": "sdxl_img2img"}`: a reserved key like `kind`); the preset names the plain workflow and the service picks the mapped one. A mapped workflow keeps every named input of its base and adds `reference_image` (set by the service) and the mode's weight input (`denoise`), so `params` work with or without a reference (`load_workflow` checks it). A workflow with `reference_image` and no map serves its own mode and is refused without a reference. `params` cannot hold the weight input when a reference runs. A picture with an alpha is laid on a flat light grey first, so a cut-out's transparent pixels are not black.
- **A draft** is a preset's content sent inline (the editor's form, saved or not): `parse_preset` checks it like a file, errors name `the draft of '<form name>'`, and its provenance says `preset: draft`.
- **The form is driven by the workflow.** `Workflow.describe(choices)` gives every named input its graph default and, for a combo input, the generator's `choices` (`/object_info`), so a new input gets its editor field without code.
- Models and LoRAs a preset names must be on the generator's machine; the manifest lists the engine's, a game lists its own extras with their licence.

## Generators

`status()` answers `ready`, `not_running` (installed or remote, not answering: offer **Start**) or `not_installed` (no install, or a manifest model missing: offer **Set up**) with a reason; the service refuses a request unless it is `ready`. `ComfyUIGenerator` finds a local install at `artgen_setup.default_root()` and says `not_running` for another host (it cannot know). A run queues the graph with a `client_id`, follows its progress on `/ws` (polling `/history` when the stream fails) and fetches the `output` images by `/view`. `upload_image(data, name)` sends a reference picture to ComfyUI's input folder (`POST /upload/image`) and returns the name for the graph; the service names it by the hash of its bytes, so one picture is one file and a remote ComfyUI needs no shared folder. A cancel, a timeout or Ctrl+C deletes the prompt from the queue and interrupts it by its id, so the GPU is freed and another client's run is left alone.

## The service

One prompt per image or sound, seeds `seed, seed + 1, ...` (random first seed when none is given), so every image can be remade alone from its provenance. `<stem>_<seed>.png` (a sound: `.flac`, the generator's own extension) and `<stem>_<seed>.artgen.json` go into the output folder (default `<game root>/artgen/out`; Accept in the editor moves the chosen one into the assets). The provenance holds the preset (name or `draft`, source `game` / `engine` / `draft`, content as `presetData`, hash of the file's bytes or of a draft's canonical JSON), the request's subject, final prompt, negative, seed, size and params, the model files the graph loads, the workflow and its hash, the generator (name, version, address), the file name, the date and the `kind`; for a sound the asked `duration` and what the FLAC holds (`sampleRate`, `channels`, `seconds`). What is done stays when a later image fails or is cancelled. A set's job goes into `<game root>/artgen/out/<set>/<job>/` beside the set's `manifest.json` (`src/Widgets/ArtGen/Sets/CLAUDE.md`).

**The verdict.** An image variant with an alpha channel gets `checks` in its provenance (`passed`, `failed` names, `metrics`, the `thresholds` used) and `{"passed", "failed"}` in its `image` event; without alpha (an icon) the checks do not apply and there is no key. A picture that cannot be read gets `{"passed": null, "error"}`. A failed check is data, never an error of the run; only the alpha is read, so a bad drawing passes.

**Extra images.** After the `count` images, while fewer than `min_pass` pass and fewer than `extra` extras were made (the request's keys, else the preset's `regenerate`, else none), one more is made at a time. Only `passed: false` fails (no alpha, or an unreadable file: a new image cannot cure it). An extra takes the first seed from `seed + count` up whose `<stem>_<seed>` file or provenance is not in the folder: the editor's `artgen_more` keeps its seed range off the row's variants, not off the extras a run makes beyond them, so no existing file is overwritten. Its events say `"extra": true` and a `count` of the images planned so far (so the editor's label never reads past its total). Short at the limit is data, the run ends `ok`.

**A reference** (the request's, else the preset's) is checked from files alone before the generator is asked anything, so a refusal writes nothing. `resolve_image` reads a relative `image` from the game root and refuses `..`, a backslash, a drive and a link out of the root (a control API request picks the path, the bytes go to another program); an absolute path is taken as it is, but not a UNC or device one (`//host/..`, `\?\`): Windows would connect to a host the client picked. The file is at most 16 MiB and a PNG, JPEG or WebP by its first bytes, not its name. The picture is uploaded once per request; the provenance gets `reference` (`mode`, `image` as asked, `hash`, `weight` as used, `workflow`) and the mapped workflow's `workflow` / `workflowHash` / `models`, so a variant can be made again alone.

## The editor bridge

```bash
python tools/artgen/artgen_bridge.py --port 8765        # the editor runs with --control (--control-port); waits, reconnects
python tools/artgen/artgen_bridge.py --port 8765 --exit-with-editor --log FILE   # what the panel's Start runs (pythonw on Windows): ends with its editor, logs to FILE
```

The contract is the editor's (`src/Widgets/ArtGen/CLAUDE.md`, "The contract"): the bridge calls `artgen_poll` about twice a second with the generator's status, the presets (the game's and the engine's, with source and content; a broken one with its error) and the workflows with their inputs of the game root the editor answered (refreshed every few seconds, at once when the answer says `refresh`: the editor saved a preset). It runs a handed-over request (a preset by name, or a draft inline: `load_request_preset`) on a worker thread with `artgen_service.generate` into `<game root>/artgen/out` and forwards its progress, each image and the end. A run that fails (a bug of ours too) ends with `artgen_done` `failed` and its reason; the bridge stays up. A cancel in a poll, a lost editor and Ctrl+C cancel the run on the generator. While the editor has no Generate panel (`unknown_method`: it started in Game mode) the bridge waits. A request may carry `minPass` and `extra` (the editor sends neither: a set job gets them from the preset's `regenerate`; a non-integer fails the run), `outDir` (a `/`-separated path of segments named like a preset: the folder of a set's job under `artgen/out`) and `name` (the file stem, the same rule); `resolve_out_dir` and `check_name` refuse anything else (`..`, an absolute path, a drive, a backslash, an empty segment) with a failed run before the generator is touched, creating nothing. A bad `reference` fails the run with its reason the same way. A game ignores `artgen/out/` in its `.gitignore`.

**Setup actions.** A `setup` in a poll's answer (`Plan`, `Install`, `Start`, `Stop`; the panel's Set up / Start / Stop) runs on a worker thread of its own through `artgen_setup.LocalSetup`, reports each event of `install --json` with `artgen_setup_report`, then `done` with `ok` and the reason, and the status is read again. `Install` plans again and installs without asking; `setupCancel` stops it between chunks (the next install resumes). Every poll also says `startedHere`: the running ComfyUI is the one `start` left (`LocalSetup.started_here`, checked once per pid), so the panel offers Stop only for it. A bridge with `--generator fake` refuses every setup action. A `shutdown` in a poll's answer (the panel's Disconnect) cancels what is in work and ends the bridge, with or without `--exit-with-editor`, which also ends it when its editor goes away or none answers in the first minute.

## Running the tests

```bash
python -m pytest tools/artgen/tests        # .venv/bin/python on macOS
```
