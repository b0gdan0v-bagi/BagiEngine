# Art Generation Tools

## Overview

The image generator of the games' art is **ComfyUI on the owner's Windows PC** (RTX 3070, 8 GB VRAM). This folder installs it, starts it and checks it; the service that turns a game's style preset into images (entry `art-gen-service`) will live here too and talks to ComfyUI over its HTTP API. Nothing here is built or linked into the engine: ComfyUI (GPL-3.0) is a separate program, and its models stay on that PC, never in git. Python 3.10+, standard library only.

| File | Purpose |
|------|---------|
| `comfyui/manifest.json` | What is installed: the pinned portable release (URL, size, SHA256) and every model (folder, pinned Hugging Face commit URL, size, SHA256, licence, source). The one place to change a version or add a model |
| `comfyui/install.ps1` | Installs the release into `-Root` (default `D:\ComfyUI`) and downloads the models, each checked against the manifest. Idempotent: a second run only verifies |
| `comfyui/start-local.bat` | Starts ComfyUI on `127.0.0.1:8188` without a browser; the installer copies it into the root. Extra arguments go to ComfyUI (`--lowvram`) |
| `comfyui_check.py` | The health check: reachable, version, devices, every manifest model present; `--generate` makes one image; `--json` for the service and the editor panel |
| `workflows/sdxl_basic.json` | The SDXL test graph in ComfyUI's API format (what `POST /prompt` takes; the UI also loads it by drag and drop) |
| `tests/` | pytest against a fake ComfyUI, and the manifest and workflow rules |

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

## Running the tests

```bash
python -m pytest tools/artgen/tests        # .venv/bin/python on macOS
```
