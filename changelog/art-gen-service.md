# Art generation: no service turns a prompt and a game's style preset into images (ComfyUI adapter, control API client, MCP tool)

- Version: 0.1.5
- Entry: art-gen-service
- Date: 2026-11-16
- Bump: patch

## Added
- `tools/artgen/`: style presets of a game (`<game>/artgen/presets/<name>.json`: a prompt template with `{subject}`, negative, size and named inputs of a workflow; `tools/artgen/CLAUDE.md`, "Presets"), workflows in ComfyUI's API format with their `<name>.inputs.json`, and the service `artgen_service.generate()`: N images of one preset and subject, one seed each, every PNG written with its `.artgen.json` provenance.
- The generator interface with the states `ready` / `not_running` / `not_installed`, `ComfyUIGenerator` (progress over its WebSocket, cancel and timeout delete and interrupt the prompt) and `FakeGenerator`; `artgen.py status | presets | generate` on the command line.
