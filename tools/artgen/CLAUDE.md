# Art Generation Tools

## Overview

The image generator of the games' art is **ComfyUI on the owner's Windows PC** (RTX 3070, 8 GB VRAM). This folder installs it, starts it and checks it, and holds the service that turns a game's style preset and a subject into images. The editor and the agent reach the generator only through that service; it talks to ComfyUI over its HTTP API and its WebSocket. Nothing here is built or linked into the engine: ComfyUI (GPL-3.0) is a separate program, and its models stay on that PC, never in git. Python 3.10+, standard library only.

| File | Purpose |
|------|---------|
| `comfyui/manifest.json` | What is installed: the pinned portable release (URL, size, SHA256) and every model (folder, pinned Hugging Face commit URL, size, SHA256, licence, source). The one place to change a version or add a model |
| `comfyui/install.ps1` | Installs the release into `-Root` (default `D:\ComfyUI`) and downloads the models, each checked against the manifest. Idempotent: a second run only verifies |
| `comfyui/start-local.bat` | Starts ComfyUI on `127.0.0.1:8188` without a browser; the installer copies it into the root. Extra arguments go to ComfyUI (`--lowvram`) |
| `comfyui_check.py` | The health check: reachable, version, devices, every manifest model present; `--generate` makes one image; `--json` for the service and the editor panel |
| `artgen_bridge.py` | The link to the editor's Generate panel: a control API client that pulls requests (`artgen_poll`) and reports progress, images and the end |
| `artgen.py` | The service on the command line: `status`, `presets`, `generate` (`--json` prints one progress event per line, then `done`) |
| `artgen_service.py` | `generate(request, generator, on_progress, cancel)`: N images of one preset and subject, each written with its provenance file as soon as it is there |
| `artgen_preset.py` | Presets and workflows: load and check them, `build_graph` (the graph of one image), `models_of` |
| `artgen_generators.py` | The generator interface (`status`, `info`, `run`), `ComfyUIGenerator`, `FakeGenerator` (tests, `--generator fake`), `make_generator` |
| `artgen_ws.py` | A minimal WebSocket client for ComfyUI's progress stream; the frame codec is `tools/control/bagi_control_client.py`'s |
| `workflows/` | The engine's workflows: `<name>.json` is a graph in ComfyUI's API format (what `POST /prompt` takes; the UI loads it by drag and drop), `<name>.inputs.json` names its inputs. `sdxl_basic` is the SDXL one |
| `tests/` | pytest: a fake ComfyUI with a real WebSocket stream, the fake generator, the presets, the manifest and workflow rules |

## Setup on the Windows PC

```powershell
powershell -ExecutionPolicy Bypass -File tools\artgen\comfyui\install.ps1 -Root D:\ComfyUI   # about 9 GB the first time
D:\ComfyUI\start-local.bat                                                                    # leave the window open
python tools\artgen\comfyui_check.py --generate                                               # exit 0: ready
```

- **Local only.** ComfyUI listens on `127.0.0.1`: no LAN port, no firewall rule (the PC is on a network that is not the owner's). If Windows asks to let Python through the firewall, cancel. The Mac reaches it through the todo service and this machine's worker agent (entry `comfyui-remote-via-todo`), never by a port.
- **Started by hand.** No service, no scheduled task: the GPU is free while ComfyUI is closed, and the check says `unreachable` (exit 2) until it is started.
- **The version is pinned.** Never run the `update\*.bat` of the portable folder: it moves ComfyUI off the manifest. To upgrade, change `comfyui` in the manifest (URL, size and the `sha256` GitHub shows in the release API's `digest`) and install into a new root.
- The installer never overwrites or deletes what it did not download: another ComfyUI version in the root, a non-empty root or a model file with another hash stops it with what to do. A broken download stays as `<file>.part` and is resumed by the next run.
- The portable release opens its UI with a Z-Image Turbo template whose "Download all" wants 19 GB of models that do not fit 8 GB: do not use it; load `workflows/sdxl_basic.json` instead.

## Models

Only models whose licence lets the generated images ship in a commercial game, each with its licence and source in the manifest. A fine-tune or a LoRA is added only after its licence was read for that; Flux and other models above 8 GB are out for now. A Hugging Face URL names a commit, not `main`, so the hash stays true (a test holds it).

## The health check

`comfyui_check.py` reads the address from `--url`, else `COMFYUI_URL`, else `comfyuiUrl` of the git-ignored `tools/artgen/local.json`, else `http://127.0.0.1:8188`. Exit 0 ready, 1 reachable but not ready (a model missing, the generation failed), 2 unreachable. Another ComfyUI version or no CUDA device is a warning. `--generate` puts a fresh seed into every sampler: ComfyUI caches node results, and a repeated graph would return at once without touching the GPU.

## Presets

A game's style lives in its own repository: `<game root>/artgen/presets/<name>.json`.

```json
{"preset": 1, "title": "Carved wooden pieces", "workflow": "sdxl_basic",
 "prompt": "a carved wooden chess {subject} piece, studio lighting, plain background",
 "negative": "blurry, text", "size": [1024, 1024], "params": {"steps": 25, "cfg": 7.0}}
```

- `preset` is the format version; `workflow` and `prompt` are required. `{subject}` in the prompt is replaced by the request's subject (without it the subject goes first). `size` sets the workflow's `width` / `height`.
- `params` sets any other named input of the workflow (`steps`, `cfg`, `sampler`, `scheduler`, `checkpoint`, `vae` for `sdxl_basic`). A name the workflow does not have, or one the service sets (`prompt`, `negative`, `seed`, `width`, `height`), is refused with the file named, so a typo never silently changes nothing.
- The workflow is looked up in `<game root>/artgen/workflows/` first, then here: a game brings its own graph (a LoRA loader, img2img) with its own `.inputs.json`, and the preset stays node-id free. The `.inputs.json` must name `prompt`, `seed` and `output` (the node whose images are the result; its `filename_prefix` is set by the service).
- Models and LoRAs named by a preset must be on the generator's machine; the manifest holds the ones the engine knows, and a game's extra model is the game's to list with its licence.

## Generators

`status()` answers `ready`, `not_running` (installed or remote, not answering: offer **Start**) or `not_installed` (no install, or a manifest model missing: offer **Set up**, entry `artgen-setup-pipelines`), with a reason; the service refuses a request unless the state is `ready`. `ComfyUIGenerator` finds a local install by `comfyuiRoot` of `local.json` (default `D:\ComfyUI` on Windows); for another host it cannot know and says `not_running`. A run queues the graph with a `client_id`, follows its progress on `/ws` (falling back to polling `/history` when the stream fails), and fetches the `output` images by `/view` (previews of type `temp` are skipped). A cancel, a timeout or Ctrl+C deletes the prompt from the queue and interrupts it by its id, so ComfyUI does not keep the GPU busy for nobody and another client's run is left alone.

## The service

One prompt per image, seeds `seed, seed + 1, ...` (random first seed when none is given), so every image can be made again alone from its provenance. `<stem>_<seed>.png` and `<stem>_<seed>.artgen.json` go into the output folder (default `<game root>/artgen/out`; the editor's Accept moves the chosen one into the game's assets). The provenance holds the preset and its file hash, subject, final prompt, negative, seed, size, params, the model files the graph loads, the workflow and its hash, the generator (name, version, address), its own file name and the date. What is done stays on disk when a later image fails or is cancelled.

## The editor bridge

```bash
python tools/artgen/artgen_bridge.py --port 8765        # the editor started with --control (--control-port); waits and reconnects
```

The contract is the editor's (`src/Widgets/ArtGen/CLAUDE.md`, "The contract"): the bridge calls `artgen_poll` about twice a second with the generator's status (refreshed every few seconds) and the presets of the game root the editor answered, runs a handed-over request on a worker thread with `artgen_service.generate` into `<game root>/artgen/out`, and forwards its progress, each image and the end. A run that fails (a missing preset, the generator not ready, a ComfyUI error, a bug of ours) ends with `artgen_done` `failed` and its reason; the bridge stays up. A cancel seen in a poll, a lost editor and Ctrl+C cancel the run on the generator. While the editor has no Generate panel (`unknown_method`: it started in Game mode) the bridge waits. A game ignores `artgen/out/` in its `.gitignore`.

## Running the tests

```bash
python -m pytest tools/artgen/tests        # .venv/bin/python on macOS
```
