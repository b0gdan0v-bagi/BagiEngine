# Art generation: no documented ComfyUI setup on the Windows PC (portable, local only, SDXL)

- Version: 0.1.3
- Entry: comfyui-windows-setup
- Date: 2026-11-16
- Bump: patch

## Added
- `tools/artgen/`: the ComfyUI used for art generation. `comfyui/manifest.json` pins the portable release and the models (commit URLs, sizes, SHA256, licences); `comfyui/install.ps1` installs them on a Windows PC with an NVIDIA GPU and verifies every download (idempotent); `comfyui/start-local.bat` starts ComfyUI on 127.0.0.1 only.
- `tools/artgen/comfyui_check.py`: the health check of that ComfyUI (version, devices, every manifest model; exit 0 / 1 / 2, `--json`, `--generate` for one real image); `workflows/sdxl_basic.json` is the SDXL test graph in ComfyUI's API format.
