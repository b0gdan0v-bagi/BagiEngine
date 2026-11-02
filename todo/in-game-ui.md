---
status: in progress since 2026-11-01. Stages 0 to 3 of the plan are done (renderer primitives, text, input, canvases and adaptive layout with the size matrix); stage 4 has started: stage 4 is done (elements and variants are reflected, screens can live in `.ui.xml` files, panel, image, label, button and stack exist, labels follow `UIVars`, a screen is tested on the size matrix; Match3 has a main menu and an engine-drawn HUD, Chess has an engine-drawn HUD); stage 5 (the authoring spike) is done and the owner chose A with B for HUDs and menus and RmlUi for scrolling, text input and animation (2026-11-02); stage 6 widens both.
stage: 3
plan: magical-leaping-blossom.md
id: no-in-game-ui-ui-exists-only-as-imgui-editor-panels
---
# No in-game UI: UI exists only as ImGui editor panels

**Where:** `src/Widgets/` (editor widgets), `Scene/Components/ClickableComponent.cpp` (the only clickable thing on a scene).

**What is missing** (found by reading the code, not by running a game): the pieces that the first screens did not need (scrolling, sliders, toggles, a progress bar, focus and gamepad navigation, text input, a promotion picker for Chess, animation and sound of the controls; plan stage 6), the debug overlay and the scene editor's tree view (stage 7; the control API methods `ui_tree` and `ui_click` exist). The controls, the XML front-end, `UIVars`, the validator and the first screens (the menu and HUD of Match3, the HUD of Chess) exist (`src/Modules/BECore/UI/CLAUDE.md`, `games/Match3/CLAUDE.md`, `games/Chess/CLAUDE.md`). Those screens were looked at in headless screenshots of the SDL software renderer (the text, the buttons, the HUD in the margins, the score and the status following the game); nobody has looked at them on Vulkan or in a real window, and the agent session cannot see a screen.

**A fix has to cover:**

- the manual look (owner): `BagiMatch3` and `BagiChess` in a real window on the Vulkan backend, and the editor viewport presets (the checks listed in the plan);
- the rest of the control set and the tooling above, which wait for the games that need them (Mahjong).
