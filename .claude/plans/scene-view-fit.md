# Scene view: fit the scene to the window

## Context

The Chess board is 640 units wide and the window is about 1632x910: the scene has no size of its own, one scene unit is one pixel of the render output, so the board fills only a part of the window. `SceneRenderWidget` says it outright: "window coordinates are scene coordinates". The Vulkan swapchain is also never recreated on resize (`AcquireFrame()` has a TODO), so the output size cannot be assumed to be the config's 1920x1080 either.

A general fix, not a Chess-only layout: a scene declares its view size in scene units (`viewWidth`, `viewHeight`); the renderer maps that view to the output with one uniform scale and centers it (bars on the free sides). Every game and the editor viewport get it the same way, and a scene that declares nothing keeps 1 unit = 1 pixel.

## Design

- `BECore/Renderer/ViewTransform.h`: `scale`, `offsetX`, `offsetY` (identity by default). `Fit(viewW, viewH, outW, outH)` builds the letterbox fit, identity when a size is not positive. `ToOutput(x, y, w, h)` maps a rect to output pixels, `ToViewX/ToViewY` map a pixel back.
- `IRenderer`: `GetOutputWidth()` / `GetOutputHeight()` (size of the current target in pixels: swapchain or offscreen), `SetViewTransform()` / `GetViewTransform()` (state held in the base class). `DrawFilledRect` and `DrawTexture` of both backends apply the transform on the CPU before drawing, so the shaders and pipelines are untouched.
- `Scene`: reflected `_viewWidth`, `_viewHeight` (XML `viewWidth`, `viewHeight`, 0 = off). `Draw()` computes the fit from the renderer's output size, sets it for the draw pass, resets it afterwards and keeps it for input. `ScreenToSceneX/Y` map a window (or viewport-local) mouse position back to scene units; without a draw yet, or without a renderer, they are the identity.
- Mouse consumers use the mapping: `ClickableComponent`, `ChessBoardComponent`, `Match3BoardComponent`.
- Scene editor `SaveScene()` writes `viewWidth` / `viewHeight` when they are not 0, so a save does not drop them.
- Chess `ChessScene`: `viewWidth="840" viewHeight="760"` (the board 640x640 at (100, 60) with the same margin on every side). Match3 is left as it is.

## Not changing

- Swapchain recreation on resize (separate TODO in the Vulkan backend).
- HiDPI: the window has no `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so output pixels are window points, as before.
- The atlas cell 116.67x190.5 is still drawn into a square (see the previous plan).

## Tasks

1. `ViewTransform.h` + gtest.
2. `IRenderer` + `VulkanRendererBackend` + `SDLRendererBackend`.
3. `Scene` fields, `Draw()`, `ScreenToScene*`; gtest for the XML fields and the identity cases.
4. Mouse consumers, scene editor save.
5. `games/Chess/config/SceneConfig.xml`.
6. Docs: `Scene/CLAUDE.md`, `rendering-module.md`, `Vulkan/CLAUDE.md`, `SDL/CLAUDE.md`, `games/Chess/CLAUDE.md`, the comments in `SceneRenderWidget.h` and `ViewportWidget.cpp`.
7. Re-configure (reflection changes), build the five targets, `ctest --preset macos-debug`, `clang-format`, `strip-pch-includes`.
8. Run `BagiChess` and check the log (the picture itself cannot be seen from the agent session).

## Status (done)

- Tasks 1-7 done: `ViewTransform`, `IRenderer` (`GetOutputWidth/Height`, `Set/GetViewTransform`), both backends, `Scene` (`viewWidth`/`viewHeight`, `Draw()` fit, `ScreenToScene*`), the three mouse consumers, the editor save, `ChessScene` view 840x760, docs.
- 15 new gtest cases (`ViewTransformTest.cpp`, `SceneViewTest.cpp` with a fake `IRenderer`); build of the five targets is clean and `ctest --preset macos-debug` passes 299/299 (was 284).
- `BagiChess` starts with a clean log. Not verified: the picture itself (no screenshot access from the agent session; the `RemoteControl` transport does not exist yet).
- Formatting: `format_sources.py --changed` was run; the unrelated reformatting it did in `ViewportWidget.cpp` was reverted to keep the diff to the comment change. `src/Modules/Vulkan` is in `.clang-format-ignore` and keeps its own alignment style.
- Match3 is left without a view size (identity), only its mouse handler uses the mapping. The open problems (swapchain recreation and HiDPI, stretched Chess pieces, the Match3 view size, the editor saving only the active scene) are entries in `TODO.md`.
