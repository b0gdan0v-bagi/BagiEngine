# SDL Module Guidelines

## Overview

SDL3 integration: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement the interfaces in `BECore/Renderer` (see `.claude/rules/rendering-module.md` for texture pipeline, serialization and fallback rules).

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: takes the reflected `ApplicationConfig` (`Create(const ApplicationConfig&, const ApplicationOptions&)`) and creates the window from the config file named by `path` (an empty `path` is a logged error), the renderer named by `renderer` (default `SDLRendererBackend`, also when `renderer` is empty or the run is headless) and the events provider |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate. `SetHeadless(true)` before `Initialize()` makes it a headless window (see "Headless and screenshots"). `GetWidth()` / `GetHeight()` read the live size in window units (`SDL_GetWindowSize`; the numbers of the config only while there is no window or SDL cannot tell, and 0 after `Destroy()`). `GetPixelDensity()`, `GetDisplayScale()` and `GetSafeArea()` read the live window (`SDL_GetWindowPixelDensity`, `SDL_GetWindowDisplayScale`, `SDL_GetWindowSafeArea`) and fall back to the neutral values of `IMainWindow` (1, 1, the whole window) when SDL fails; see "Input and window metrics" |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends. `GetOutputWidth/Height()` is `SDL_GetCurrentRenderOutputSize`; the draw calls apply `IRenderer::GetViewTransform()` to their rects; rects blend with `SDL_BLENDMODE_BLEND`; `DrawTexture` sets the color and alpha modulation of the texture from the tint on every draw; the clip stack is applied through `SDL_SetRenderClipRect` in `OnClipRectChanged()` (grown to whole pixels); `CreateTexture()` makes a static `RGBA32` texture; `DrawGeometry()` is `SDL_RenderGeometryRaw` over scratch arrays that it keeps between calls (the view transform and the translation are applied to the positions on the CPU, the clip is the render clip); `RequestCapture()` reads the pixels back at the present |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events; see "Input and window metrics" |
| `SDLScreenshot` | `EncodePng()` (scale down, PNG in memory) and `RegisterScreenshotControl()`: the `screenshot` method of the control API |
| `SDLTexture` / `SDLTextureLoader` | `ITexture` implementation and its `IResourceLoader` |
| `SDLWindowConfig` | Window settings (`config/SDLWindowConfig.xml`: title, size, `windowFlags`; the shipped file asks for `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so a Retina display gets a window with two output pixels per unit; the headless dummy driver stays at 1) |
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

## Input and window metrics

`SDLEventsProvider::ConvertSDLEvent` turns SDL events into `SDLEvents` (`SDLEvents.h`), after emitting every one of them raw as `SDLEventWrapper` (ImGui reads those):

| SDL event | Engine event | Kept back while |
|-----------|--------------|-----------------|
| mouse button down / up, motion, wheel | `MouseButtonDownEvent`, `MouseButtonUpEvent`, `MouseMotionEvent`, `MouseWheelEvent`, **through `UIInput`** (below) | an ImGui context exists and `WantCaptureMouse` |
| key down / up | `KeyDownEvent` (key, modifiers, repeat), `KeyUpEvent` | an ImGui context exists and `WantCaptureKeyboard` |
| text input | `TextInputEvent` (UTF-8, copied: SDL frees its string) | the same keyboard rule; SDL sends it only between `SDL_StartTextInput` and `SDL_StopTextInput`, which nothing calls yet |
| window resized, pixel size or display scale changed, moved, focus gained / lost, close requested | `WindowEvent` (`window.type` says which) | |
| `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` | `SafeAreaChangedEvent` (no payload: read `IMainWindow::GetSafeArea()`) | |
| `SDL_EVENT_DISPLAY_ORIENTATION` | `DisplayOrientationChangedEvent` (display id, `SDL_DisplayOrientation`) | |

Game run mode has no ImGui context, so nothing is kept back there. Mouse positions are in output pixels, not scene units (`Scene::ScreenToSceneX/Y` maps them): SDL reports them in window units, and the provider multiplies the position of a button, motion (`x`, `y`, `xrel`, `yrel`) and wheel event (`mouse_x`, `mouse_y`; the scroll amount stays) by `IMainWindow::GetPixelDensity()` (1 with no window) before `UIInput` sees it, reading the density for every event because a window can move to another display. The raw `SDLEventWrapper` that ImGui reads stays in window units (ImGui has its own framebuffer scale), and so does the viewport-local position that `ViewportWidget` re-emits in the editor.

**`UIInput`** (`BECore/Input/UIInput.h`, `CoreManager::GetUIInput()`) is the one door for mouse input. The provider and `ViewportWidget` do not emit the mouse events themselves: they call `DispatchMouseButtonDown/Up`, `DispatchMouseMotion`, `DispatchMouseWheel`. Registered `IUIInputHandler`s (the UI canvases, `UICanvasComponent`, `BECore/UI/CLAUDE.md`) are asked first, the higher `order` first and, among equals, the later registered one; the first that answers `true` consumes the event, and only an event nobody consumed is emitted for game code. A click on a button therefore does not also hit the board under it. A handler that holds a pressed button answers `true` for the matching up and for the motion in between, wherever the pointer is. Registering or unregistering inside a handler is safe: it takes effect after the dispatch in progress, and an unregistered handler is not called again.

`ViewportWidget` (Editor mode) feeds the same door with viewport-local positions: down only while the image is hovered, up and motion also after the pointer leaves the image, as long as the press began there; wheel while hovered.

**Window metrics** (`IMainWindow`): `GetWidth()` / `GetHeight()` and `GetSafeArea()` are in window units, `GetPixelDensity()` is output pixels per window unit (2 on a Retina display: the shipped window config asks for the high-density mode, and both renderer backends follow the window's pixel size, the Vulkan swapchain is recreated on resize, `Vulkan/CLAUDE.md`), `GetDisplayScale()` is the user's display scale. Layout takes all three as inputs. The headless dummy driver answers 1, 1 and the whole window. A notch or a rotation cannot be produced on a desktop, so those paths are covered by injected events only.

Tests: `tests/BECore/SDLEventsProviderTest.cpp` (conversions and both capture rules, with the real SDL queue), `UIInputTest.cpp` (order, consume, deferred changes), `WindowMetricsTest.cpp`.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame:

1. `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`.
2. `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and then `ImGui_ImplSDLRenderer3_NewFrame()`.
3. `ImGuiWidget` calls `ImGui::NewFrame()`, and the widgets build their windows.
4. `ImGuiWidget` calls `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`; `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events, so widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures through logging/assertions. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) over silent failure. Resolve asset paths through the `FileSystem`, not ad-hoc relative paths.
