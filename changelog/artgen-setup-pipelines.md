# Art generation: ComfyUI cannot be set up from the editor, and only on Windows + NVIDIA (general setup pipelines offered by the Generate panel)

- Version: 0.1.8
- Entry: artgen-setup-pipelines
- Date: 2026-11-16
- Bump: patch

## Added
- `tools/artgen/artgen_setup.py`: one ComfyUI setup pipeline for every platform: `detect` (Windows NVIDIA / ARM64 / AMD / Intel, macOS on Apple Silicon, Linux with NVIDIA), `install` (prints every download with size and licence, asks, resumes, checks hashes; idempotent), `start` / `stop` of a background ComfyUI, `status`.
- The Generate panel sets the generator up: Start the service (turns the control API on and starts the bridge in the background), Set up (the plan, then Download and install with progress and Cancel), Start, Stop ComfyUI (only one started from the panel), Disconnect.
- Control methods `artgen_setup` and `artgen_setup_report`; `artgen_poll` answers `setup`, `setupCancel` and `shutdown` and takes `startedHere`; `artgen_state` has `setup`.
- `ApplicationEvents::StartControlServerRequestEvent`: a panel asks for the control API while it is off (the default port, else a free one).
- `artgen_bridge.py`: `--exit-with-editor`, `--log FILE`.

## Changed
- `tools/artgen/comfyui/manifest.json` is format 2: `comfyui.platforms` holds a variant per platform (a portable release asset, or the pinned commit and a venv). `comfyui/install.ps1` is gone. Adapt: run `python tools/artgen/artgen_setup.py install` instead; an existing install is found and only checked.
