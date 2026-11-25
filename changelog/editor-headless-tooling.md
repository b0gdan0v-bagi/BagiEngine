# Headless editor: agents operate the editor's ImGui panels; artgen, QA, screenshots and perf run it without a window

- Version: 0.24.2
- Entry: editor-headless-tooling
- Date: 2026-11-25
- Bump: patch

## Added
- Control methods `imgui_find` (an ImGui window or item by label or id, with its rect) and `imgui_input` (mouse, wheel, keys and text fed to the next frames, on the manual clock); ImGui's clock follows the manual clock; `GameSession` and `bagi_mcp.py` helpers for click, type, key and wheel (`.claude/rules/control-imgui-api.md`). Adapt: nothing.
- Tools: artgen starts a headless editor (`tools/artgen/headless_check.py`); `/qa` drives the editor (`QaSession` editor moves, editor charters, `bagi_qa_scene_file.py` compares a saved scene file); the engine's screenshot set covers the editor's panels; the perf set has `editor-frame` (`tools/perf/editor_frames.py`) and a benchmark may name its own preset. Adapt: nothing; a game's `qa-project.md` and `perf-project.md` may add editor charters, shots and the benchmark.
