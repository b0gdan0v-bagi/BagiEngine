---
status: in progress since 2026-11-01. Stage 5 of the plan is the spike that measures the options, and it is done: A (the engine's XML), B (a web-style HTML + CSS front-end, `.ui.html`) and C (RmlUi, `src/Modules/RmlUiHost`, behind `-DENGINE_UI_RMLUI=ON`, off by default) are built and measured, with all three shipped screens written in each (the same rects as the XML on all 138 cases of the size matrix), a 1,001-element screen in each, and the cost of C in binary size, heap, layout time and build time (`src/Modules/BECore/UI/AuthoringDecision.md`). **Decided on 2026-11-02: A with B on the element tree for HUDs and menus, and RmlUi for the screens that need scrolling, text input or animation** (the decision section of the record). Stage 6 builds what RmlUi lacks to ship: `DrawGeometry` in the Vulkan backend (done 2026-11-02, so a document draws on both backends), the font engine (done 2026-11-02: `RmlUiFontEngine` over the engine's own `Font`, no FreeType), hot reload of RmlUi documents (done 2026-11-02: the draw polls the files of a document, `hotReloadInterval` in `UIConfig.xml`), then the small controls of the element tree.
stage: 3
plan: magical-leaping-blossom.md
id: game-ui-has-no-llm-friendly-authoring-path-yet-choose-one
---
# Game UI has no LLM-friendly authoring path yet: choose one before building it

**Where:** `src/Widgets/` (the editor panels are ImGui code), `games/*/config/SceneConfig.xml` and `Scene/CLAUDE.md` (scenes are XML; positions are absolute pixels in `TransformComponent`), [the control API entry](control-api-gaps.md) (a screenshot is planned, not built). No Figma or other UI import exists in the repository.

**What is wrong** (found by reading the code, not by running anything): in-game UI does not exist yet ([the entry on in-game UI](in-game-ui.md)), so its authoring format is still free to choose. A model can edit XML as text, but it cannot see the result until the screenshot exists, a designer's mockup has no path into the engine, and absolute pixel positions break on other screen sizes (Android, web).

**Options** (a decision of the project owner):

1. **Declarative UI in the engine's own XML** (panel, label, button, anchors), edited as text and checked by a control API screenshot and a scene dump. No new dependency; it fits reflection and the scene editor.
2. **Figma as the source:** (a) the model reads the design through the Figma MCP server and writes the XML itself; or (b) a script in `tools/` imports a Figma file through the Figma REST API (frames to panels, text to labels, components to prefabs, exported images to an atlas) with a layer naming convention as the contract. Both need the text, layout and prefab pieces first.
3. **An HTML/CSS-like UI library** (RmlUi is the candidate; whether it fits all three release targets is not checked). Models know HTML and CSS well, but it is a large dependency with its own layout and text.
4. **Dear ImGui for game UI** stays what it is today: for tools. It is code, not data, and [the in-game UI entry](in-game-ui.md) puts game UI on `IRenderer`.

Recommendation: option 1, built for a model's edit, run, screenshot loop. Option 2(a) can be tried at once with no engine work; 2(b) only when real Figma files exist.

**A fix has to cover:**

- the chosen path recorded in `Scene/CLAUDE.md` (or a UI doc next to the code);
- UI in plain text files with stable node names and no editor-only state, so a model can read and diff them;
- the feedback loop: screenshot and scene dump from the control API, and hot-reload of scene files in Game mode ([the saves / hot-reload entry](save-hot-reload-packing.md));
- layout (anchors, sizes) computed in pure code and covered by gtests at several aspect ratios, without a window;
- a short "how to build a screen" section with the component list and one example screen in the nested `CLAUDE.md`, the place models read;
- if Figma is chosen: the layer naming convention, the importer in `tools/` and a fixture file in a test.
