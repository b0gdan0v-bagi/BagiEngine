# A guide for people: docs/guide/ with 18 pages checked against the code

- Version: 0.31.2
- Entry: human-docs
- Merge: 2
- Date: 2026-11-27
- Bump: patch

## Added
- `docs/guide/`: a guide for a person new to the engine, in English, 18 pages, each a task end to end: run the demo, the first scene, a UI screen, a prefab, a tween and a timeline, a config and reflection, input actions, a save, the editor, the web build, the control API from Python, a test of a game, working with agents (ask for a change, the todo loop, the skills, what to review), the C++ rules explained for a Python developer, and where the data lives for a designer. `README.md` links it; it is outside the docs budget and not loaded into agents' context. Adapt: nothing.
- ctest `BagiGuideCheck` (`tools/docs/check_guide.py`): every code block of a page is an excerpt of a real file, a command whose paths and presets exist, or marked as an illustration; links, pictures and paths resolve; the index lists every page. The rule for authors: `.claude/rules/guide-docs.md`. Adapt: nothing.
- `tools/docs/guide_shots.py` with `tools/docs/guide_shots.json`: the guide's pictures taken headless through the control API (optional `contentOverlay` per row, a clean editor folder per picture). Adapt: nothing.
- Compiled examples of the pages with their tests (`tests/BECore/Guide*Example*`, `tools/demo/tests/DemoGuide*`), the pages' Python scripts as ctest entries `BagiDemoGuideScript.<name>`, and the demo's own scenario `BagiDemoScenario.title` with a reference picture. Adapt: nothing.

## Changed
- Shared test helpers: `tests/Common/ConfigReloadSupport.h` (the hot-reload recorders and fixture, used by `ConfigHotReloadTest`), free matrix helpers in `tests/Common/UIScreenTestSupport.h`, and `DemoSceneFixture::LoadScene` takes an optional scene file. Adapt: a game test that copied the old `UIPanelScreenTest` members still compiles; nothing to change.
- `README.md` names three more principles (what a game reflects is reachable from outside, headless and deterministic, data errors are visible). `tools/control/CLAUDE.md` installs the control requirements with `uv pip install --python .venv/bin/python -r tools/control/requirements.txt` (a venv made by `uv` has no pip). Adapt: nothing.
