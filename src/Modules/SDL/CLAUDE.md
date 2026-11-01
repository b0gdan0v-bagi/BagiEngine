# SDL Module Guidelines

## Overview

SDL3 integration: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement the interfaces in `BECore/Renderer` (see `.claude/rules/rendering-module.md` for texture pipeline, serialization and fallback rules).

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: creates the window from `SDLWindowConfig.xml`, the renderer named by `renderer` in `config/ApplicationConfig.xml` (default `SDLRendererBackend`) and the events provider |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate. `SetHeadless(true)` before `Initialize()` makes it a headless window (see "Headless and screenshots") |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends. `GetOutputWidth/Height()` is `SDL_GetCurrentRenderOutputSize`; the draw calls apply `IRenderer::GetViewTransform()` to their rects; `RequestCapture()` reads the pixels back at the present |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events. A mouse-button-down goes to game code unchanged (output pixels, not scene units: `Scene::ScreenToSceneX/Y` maps them) unless an ImGui context exists and wants the mouse; Game run mode has no context, so every click goes through (`tests/BECore/SDLEventsProviderTest.cpp`) |
| `SDLScreenshot` | `EncodePng()` (scale down, PNG in memory) and `RegisterScreenshotControl()`: the `screenshot` method of the control API |
| `SDLTexture` / `SDLTextureLoader` | `ITexture` implementation and its `IResourceLoader` |
| `SDLWindowConfig` | Window settings (`config/SDLWindowConfig.xml`: title, size, `windowFlags`) |
| `SDLUtils` | Shared SDL helpers |

`IMainWindow`, `MainWindowManager` and `MainWindowAccessor` (access to the window and renderer) live in `BECore/MainWindow/`, not here.

## SDL3 vs SDL2 Differences

- Event types use the `SDL_EVENT_` prefix
- Rectangles use `SDL_FRect` (float) by default
- `SDL_Init()` returns `bool`, not `int`

## Best Practices

- **Forward declare SDL types** in headers (`struct SDL_Window;`).
- **Use accessors**: do not store raw SDL pointers long-term.
- **Check return values**: SDL3 functions may fail.
- **Use the event system**: do not poll SDL events outside `SDLEventsProvider`.
- Engine-facing code depends on `IRenderer` / `ITexture`, never on raw SDL handles.

## Headless and screenshots

`--headless` (`ApplicationOptions::headless`, set by `Application` from `ControlSettings`; `Control/CLAUDE.md`) runs a game with no window at all:

- `SDLMainWindow` sets the hint `SDL_HINT_VIDEO_DRIVER` to `dummy` before `SDL_Init` (the hint is reset on `Destroy()`, so a later window picks its driver again) and takes `SDL_WINDOW_VULKAN` out of the configured flags. The window keeps the configured size, which is also the size of the picture.
- `SDL3ApplicationFabric` ignores the `renderer` attribute and creates `SDLRendererBackend`, which asks SDL for the `software` renderer when its window is headless (the Vulkan backend needs a real surface). The texture loader follows the renderer (`SDLTextureLoader::CanLoad`), so sprites load as usual.
- Checked on macOS only: the `dummy` driver with the software renderer works, a hidden `cocoa` window with the software renderer does not (`software not available`), and the `offscreen` driver needs OpenGL. Windows is not checked.
- The Editor run mode cannot be headless (its viewport is an offscreen render target, which this backend does not implement): `Application` stops the start-up with a log line.
- A headless game has no vsync, so on the real clock it draws frames as fast as it can. Use it with `--manual-clock`.

`IRenderer::RequestCapture(callback)` is how pixels leave a renderer: `SDLRendererBackend` serves the request in `PresentFrame()` just before `SDL_RenderPresent` (`SDL_RenderReadPixels`, converted to RGBA8; after the present the back buffer is undefined) and hands a `PixelBuffer` to the callback. A newer request replaces an unserved one, whose callback gets an error. The headless run and the windowed SDL renderer both support it; `VulkanRendererBackend` does not (the default of `IRenderer` answers with an error).

The `screenshot` method (`RegisterScreenshotControl`, registered by `SDL3ApplicationFabric::Create`) asks for the capture and defers its answer by one frame of zero seconds, so on the manual clock a screenshot is a frame of its own in which nothing moves, and on the real clock it is the frame being run. It answers `{width, height, png_base64}`; `maxSize` (16 to 4096, default 1024) cuts the longer side, the proportions are kept and a picture is never enlarged. A model reads the PNG, so keep the default small. Tests: `tests/BECore/SDLCaptureTest.cpp`.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame:

1. `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`.
2. `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and then `ImGui_ImplSDLRenderer3_NewFrame()`.
3. `ImGuiWidget` calls `ImGui::NewFrame()`, and the widgets build their windows.
4. `ImGuiWidget` calls `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`; `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events, so widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures through logging/assertions. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) over silent failure. Resolve asset paths through the `FileSystem`, not ad-hoc relative paths.
