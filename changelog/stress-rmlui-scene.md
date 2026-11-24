# BagiStress gets an RmlUi scene, and the control API a generic wheel_at

- Version: 0.17.1
- Entry: stress-rmlui-scene
- Date: 2026-11-24
- Bump: patch

## Added
- The control method `wheel_at` (`BECore/Input/InputControl.h`, `{x, y, dx?, dy?, flipped?, which?}` to `{x, y, consumed}`): one mouse wheel event at an output pixel through `UIInput::DispatchMouseWheel`, so a QA charter or a test can scroll a document or a canvas. Adapt: nothing.
- `BagiStress` has the scene `rmlui` (only in builds with `ENGINE_UI_RMLUI`): `count` RmlUi documents and a scrollable panel, with reload through `call_method` `ReloadRmlUiDocuments` and frame counters for documents, textures, reloads and scroll; `tools/stress/qa/vulkan_look.py` runs it in a window under the Vulkan validation and screenshot layers. Adapt: nothing.
