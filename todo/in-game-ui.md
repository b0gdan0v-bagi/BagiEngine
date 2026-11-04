---
status: review 2026-11-04 branch todo/no-in-game-ui-ui-exists-only-as-imgui-editor-panels: UISegmented (a radio group of UIButton segments bound to UIVars), ActionEvent and UIButton carry a value, selected look, ui_tree/ui_click, HTML+CSS front-end, a panel screen test on the size matrix; engine ctest 1094/1094, game 528/528 against this branch, slice 1 of 2 (toggle, slider, progress bar, string table, sounds are left), after the merge set the status back to open
stage: 3
plan: magical-leaping-blossom.md
id: no-in-game-ui-ui-exists-only-as-imgui-editor-panels
---
# No in-game UI: UI exists only as ImGui editor panels

**Where:** `src/Widgets/` (editor widgets), `Scene/Components/ClickableComponent.cpp` (the only clickable thing on a scene).

**What is missing** (found by reading the code, not by running a game): the pieces that the first screens did not need. The choice group exists now (`UISegmented`, stage 6.4 first part: a radio group of `UIButton` segments bound to a `UIVars` variable, a button carries a `value` with its action and a `selected` look, `ui_tree` / `ui_click` and the HTML + CSS front-end know it; a two-segment group is the toggle of a binary choice, a panel of such groups is tested on the size matrix, `UI/CLAUDE.md` "Choice panels"). What stage 6.4 still lacks: a single labeled toggle (`UIToggle`), a slider, a progress bar, the string table, button sounds and a dropdown (judged not needed while four options fit a row), and also scrolling, focus and gamepad navigation, keyboard and gamepad activation, text input, a promotion picker for Chess, animation of the controls (plan stage 6), the debug overlay and the scene editor's tree view (stage 7; the control API methods `ui_tree` and `ui_click` exist). Each waits for a screen that asks for it (Mahjong is named as the first consumer). The controls, the XML front-end, `UIVars`, the validator and the first screens (the menu and HUD of Match3, the HUD of Chess) exist (`src/Modules/BECore/UI/CLAUDE.md`, `games/Match3/CLAUDE.md`, `games/Chess/CLAUDE.md`). Those screens were looked at in headless screenshots of the SDL software renderer (the text, the buttons, the HUD in the margins, the score and the status following the game); nobody has looked at them, or at the selected-segment look (a fill plus a 4-unit bar, a first guess), on Vulkan or in a real window, and the agent session cannot see a screen. The Chess HUD does not use the choice group yet: that is the game entry `chess-hud-opponent-panel`.

**A fix has to cover:**

- the manual look (owner): `BagiMatch3` and `BagiChess` in a real window on the Vulkan backend, and the editor viewport presets (the checks listed in the plan);
- the rest of the control set and the tooling above (toggle, slider, progress bar, string table, sounds, focus), which wait for the games that need them (Mahjong).
