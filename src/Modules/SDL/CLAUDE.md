# SDL Module Guidelines

## Overview

SDL3 integration: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement `BECore/Renderer` (`.claude/rules/rendering-module.md`: texture pipeline, serialization, fallback).

On the web (Emscripten) there is no Vulkan: the window config and the renderer come from the overlay `web/config/` (`ApplicationConfig.xml` with `SDLRendererBackend`, `SDLWindowConfig.xml` without `SDL_WINDOW_VULKAN`), the last content root of `src/main.cpp` (`BAGI_WEB_ROOT`), so it wins over the game's files of the same name. A renderer name that no backend registers (`VulkanRendererBackend` in a build without `ENGINE_WITH_VULKAN`) is a logged error of `SDL3ApplicationFabric::Create`. Details, the canvas glue and tests: "Web build" below.

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: takes the reflected `ApplicationConfig` (`Create(const ApplicationConfig&, const ApplicationOptions&)`) and creates the window from the config file named by `path` (an empty `path` is a logged error), the renderer named by `renderer` (default `SDLRendererBackend`, also when `renderer` is empty or the run is headless) and the events provider |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate. `SetHeadless(true)` before `Initialize()` makes it a headless window, which answers size, density and safe area from its simulation (see "Headless and screenshots"). A real window's `GetWidth()` / `GetHeight()` read the live size in window units (the config's numbers only while there is no window or SDL cannot tell, 0 after `Destroy()`); `GetPixelDensity()`, `GetDisplayScale()` and `GetSafeArea()` read the live window and fall back to the neutral values of `IMainWindow` (1, 1, the whole window) when SDL fails (on the web `GetSafeArea()` is the insets of the page, `SDLWebCanvas`); see "Input and window metrics" |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends. `GetOutputWidth/Height()` is `SDL_GetCurrentRenderOutputSize`; the draw calls apply `IRenderer::GetViewTransform()` to their rects; rects, textures and geometry blend with the mode of `GetBlendMode()` (`ToSDLBlendMode()`: `BLEND` / `ADD` / `MUL`, set on every draw); `DrawTransformedTexture()` is `SDL_RenderTextureRotated` (an identity transform is a plain `DrawTexture`; `.claude/rules/rendering-module.md`); `DrawTexture` modulates the texture by the tint on every draw; the clip stack goes to `SDL_SetRenderClipRect` in `OnClipRectChanged()` (grown to whole pixels); `CreateTexture()` is a static `RGBA32` texture; `DrawGeometry()` is `SDL_RenderGeometryRaw` over scratch arrays that it keeps between calls (the view transform and the translation are applied to the positions on the CPU, the clip is the render clip); SDL only queues it: the software renderer rasterizes at the present (the cost shows in `SDLRenderer::PresentFrame`; `.claude/rules/ui-shapes.md`); `GetGeometryDetail()` is `Reduced` on the software renderer, `Full` on a GPU driver; `RequestCapture()` reads the pixels back at the present; `CreateRenderTarget()`: see "Render targets" |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events; see "Input and window metrics" |
| `SDLScreenshot` | `EncodePng()` (scale down, PNG in memory) and `RegisterScreenshotControl()`: the `screenshot` method of the control API |
| `SDLRenderTarget` | `IRenderTarget` over a `SDL_TEXTUREACCESS_TARGET` texture (the ImGui texture id is the `SDL_Texture*`); the renderer lists the live ones and releases their texture in `Destroy()`, so a target may outlive it |
| `SDLTexture` / `SDLTextureLoader` | `ITexture` implementation and its `IResourceLoader` |
| `ImageFile` | `ReadImageFile()`: straight RGBA for the texture loaders and the editor. A PNG goes through `stb_image`: SDL_image on Apple uses ImageIO, which premultiplies the alpha |
| `SDLWindowConfig` | Window settings (`config/SDLWindowConfig.xml`: size, `windowFlags`, an optional `title` the shipped files omit (the title is composed, see below); the shipped file asks for `SDL_WINDOW_HIGH_PIXEL_DENSITY`) |
| `SDLWebCanvas` | `FitWindowSize()` and `SafeAreaFromInsets()` (pure, every platform: the window size for a browser window, rounded, never below a minimum; the safe area from the four CSS insets) and, Emscripten only, `InstallWebCanvasResize()` and `GetWebSafeArea()`; see "Web build" |
| `SDLAudioBackend` | `BECore/Audio/CLAUDE.md` |
| `SDLClipboard` | `SDLClipboardBackend` (`SDL_SetClipboardText` / `SDL_GetClipboardText`, installed by `SDL3ApplicationFabric::Create`) and the pure `IsPasteShortcut`. The service and its rules: `BECore/Clipboard/CLAUDE.md` |
| `SDLWebPost` | `WebPostStart()` / `WebPostPoll()` (text POST to a path of the page's own origin, answered by polling), `IsWebPostPathValid()` and the limits: pure, every platform; the browser half is Emscripten only, every other build says "unavailable"; see "Web build" |
| `SDLWebDownload` | `WebDownloadStart()` (a Blob of a `HEAPU8.slice` copy, threaded memory is shared, and a clicked temporary link) and `IsWebDownloadNameValid()`; split as `SDLWebPost` |
| `SDLUtils` | Shared SDL helpers |

`IMainWindow`, `MainWindowManager` and `MainWindowAccessor` (access to the window and renderer) live in `BECore/MainWindow/`, not here.

**Window title.** `Application` calls `IMainWindow::SetTitle()` after `GameManager::Initialize()` and when the active game's name or the run mode changes, with `ComposeWindowTitle` (`BECore/MainWindow/WindowTitle.h`, pure): editor `<Game> — BagiEngine Editor`; game mode the `title` of a game's own `SDLWindowConfig.xml` (`GetConfiguredTitle()`), else the game's name, else `BagiEngine`.

## SDL3 vs SDL2 Differences

- Event types use the `SDL_EVENT_` prefix; rectangles are `SDL_FRect` (float); `SDL_Init()` returns `bool`, not `int`.

## Best Practices

- Forward declare SDL types in headers (`struct SDL_Window;`); use accessors, no raw SDL pointers kept long-term; check return values (SDL3 functions may fail).
- Do not poll SDL events outside `SDLEventsProvider`. Engine-facing code depends on `IRenderer` / `ITexture`, never on raw SDL handles.

## Headless and screenshots

`--headless` (`ApplicationOptions::headless`, set by `Application` from `ControlSettings`; `Control/CLAUDE.md`) runs a game with no window at all:

- `SDLMainWindow` sets the hint `SDL_HINT_VIDEO_DRIVER` to `dummy` before `SDL_Init` (the hint is reset on `Destroy()`, so a later window picks its driver again) and takes `SDL_WINDOW_VULKAN` out of the configured flags. The window has the configured size, or the one of `--window` / `--density` / `set_window` (`.claude/rules/control-window-api.md`); the picture is its size times the density.
- `SDL3ApplicationFabric` ignores the `renderer` attribute and creates `SDLRendererBackend`, which asks SDL for the `software` renderer when its window is headless (the Vulkan backend needs a real surface). The texture loader follows the renderer (`SDLTextureLoader::CanLoad`), so sprites load as usual.
- The `dummy` driver with the software renderer works on macOS and Windows 11; a hidden `cocoa` window with it does not (`software not available`), and `offscreen` needs OpenGL.
- The Editor run mode cannot be headless (its viewport is an offscreen render target, which this backend does not implement): `Application` stops the start-up with a log line.
- A headless game has no vsync, so on the real clock it draws as fast as it can: use `--manual-clock`.

`IRenderer::RequestCapture(callback)` is how pixels leave a renderer: `SDLRendererBackend` serves the request in `PresentFrame()` just before `SDL_RenderPresent` (`SDL_RenderReadPixels`, converted to RGBA8; after the present the back buffer is undefined) and hands a `PixelBuffer` to the callback. A newer request replaces an unserved one, whose callback gets an error. The headless run and the windowed SDL renderer support it; `VulkanRendererBackend` does not (the default of `IRenderer` answers with an error).

The `screenshot` method (`RegisterScreenshotControl`, registered by `SDL3ApplicationFabric::Create`) asks for the capture and answers one frame of zero seconds later: on the manual clock a frame of its own in which nothing moves, on the real clock the frame being run. It answers a reflected `ScreenshotAnswer` `{height, png_base64, width}` (the base64 string is moved in); `maxSize` (16 to 4096, default 1024) cuts the longer side, the proportions are kept and a picture is never enlarged. Tests: `tests/BECore/SDLCaptureTest.cpp`.

## Render targets

`SetRenderTarget(target)` is `SDL_SetRenderTarget`, then a clear with the colour of the last `Clear()` and back to opaque black (as Vulkan: the viewport calls `Clear(color)` just before it; a `Clear()` with no target still fills the back buffer at once). A target of another backend or one whose renderer is gone keeps the back buffer (one `LOG_ERROR`). `PresentFrame()` unsets a target left open first: the window is what is presented and what a capture reads, so a screenshot of the editor is the whole window. SDL keeps the clip per target. Tests: `SDLRenderTargetTest.cpp`.

## The software present allocates per rotated sprite

`SDL_RenderPresent` of the software renderer makes 5 allocations per rotated sprite per frame (3 under 512 bytes and 2 pixel buffers, all in `SDLRenderer::PresentFrame`; `SceneDrawSystem::Run` allocates nothing in a steady frame): `SW_RenderCopyEx` clones the source surface, scales or crops it into a second one and rotates it into a third. A sprite with no rotation and no flip is a plain blit with none; the GPU drivers queue a rotated quad. **Nothing cheap on the engine's side:** a quarter turn is a transpose, which no flip does, a half turn is two flips, which `COPY_EX` allocates for as well, and `DrawGeometry` is wrong for a turned texture on this renderer (`.claude/rules/rendering-module.md`, "Software rasterizer"). The code stays as it is.

## Input and window metrics

`SDLEventsProvider::ConvertSDLEvent` turns SDL events into `SDLEvents` (`SDLEvents.h`) after emitting each raw as `SDLEventWrapper` (ImGui reads those):

| SDL event | Engine event | Kept back while |
|-----------|--------------|-----------------|
| mouse button down / up, motion, wheel | `MouseButtonDownEvent`, `MouseButtonUpEvent`, `MouseMotionEvent`, `MouseWheelEvent`, **through `UIInput`** (below) | an ImGui context exists and `WantCaptureMouse` |
| finger down / motion / up (a touch screen of a window) | the same four mouse events, each finger with a `which` of its own (`BECore/Input/CLAUDE.md`, "Pointers and touch"); the mouse events SDL synthesizes from touch (`SDL_TOUCH_MOUSEID`) are dropped while SDL sends finger events | the same |
| key down / up | `KeyDownEvent` (key, modifiers, repeat), `KeyUpEvent`; Cmd+V (Apple) / Ctrl+V (else) also calls `Clipboard::RequestPaste()` | an ImGui context exists and `WantCaptureKeyboard` |
| text input | `TextInputEvent` (UTF-8, copied: SDL frees its string) | the same keyboard rule; SDL sends it only between `SDL_StartTextInput` and `SDL_StopTextInput`, which nothing calls yet |
| window resized, pixel size or display scale changed, moved, focus gained / lost, close requested | `WindowEvent` | |
| `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` | `SafeAreaChangedEvent` (no payload: read `IMainWindow::GetSafeArea()`) | |
| `SDL_EVENT_DISPLAY_ORIENTATION` | `DisplayOrientationChangedEvent` (display id, `SDL_DisplayOrientation`) | |

Mouse positions are in output pixels, not scene units (`Scene::ScreenToSceneX/Y` maps them): SDL reports them in window units, and the provider multiplies the position of a button, motion (`x`, `y`, `xrel`, `yrel`) and wheel event (`mouse_x`, `mouse_y`; the scroll amount stays) by `IMainWindow::GetPixelDensity()` (1 with no window) before `UIInput` sees it, reading the density for every event because a window can move to another display. The raw `SDLEventWrapper` that ImGui reads stays in window units (ImGui has its own framebuffer scale), and so does the viewport-local position that `ViewportWidget` re-emits in the editor.

**`UIInput`** (`BECore/Input/UIInput.h`) is the one door for mouse input: the provider and `ViewportWidget` call `DispatchMouseButtonDown/Up`, `DispatchMouseMotion`, `DispatchMouseWheel` and only an event nobody consumed is emitted for game code. The order, the shapes, the capture and the reentrancy rules: `BECore/Input/CLAUDE.md`.

**While the game is paused** (`CoreManager::GetPause().IsPaused()`, which includes the manual clock) the provider drops the game-bound branches: mouse (so `UIInput` sees nothing), key and text. They are dropped, not queued, so a click made in the pause does not fire on resume. Kept: the raw `SDLEventWrapper` (ImGui, `Ctrl+Shift+E` and `Ctrl+Shift+P` read it), `QuitEvent` and the window, safe-area and orientation events (a resize re-lays the scene). `UIInput` itself is not gated: input injected by the control API (`ui_click`, `click_at`) works in a pause. `ViewportWidget::ForwardMouse` does the same and forgets a press that began before the pause. A canvas that took a press before the pause keeps it (pressed look, consumed hover) until the next click.

`ViewportWidget` (Editor mode) feeds the same door with viewport-local positions: down only while the image is hovered, up and motion also after the pointer leaves the image, as long as the press began there; wheel while hovered.

**Window metrics** (`IMainWindow`): `GetWidth()` / `GetHeight()` and `GetSafeArea()` are in window units, `GetPixelDensity()` is output pixels per window unit (2 on a Retina display: the shipped window config asks for the high-density mode, and both renderer backends follow the window's pixel size, the Vulkan swapchain is recreated on resize, `Vulkan/CLAUDE.md`), `GetDisplayScale()` is the user's display scale. Layout takes all three as inputs. The dummy driver has no density and no notch, so a headless `SDLMainWindow` answers them from its simulation (1, 1 and the whole window until set).

Tests: `tests/BECore/SDLEventsProviderTest.cpp` (conversions, the capture and pause rules, with the real SDL queue), `UIInputTest.cpp`, `WindowMetricsTest.cpp`.

## Web build

The browser build (`ENGINE_TARGET_WEB`, presets `web-release` / `web-debug` and their `-single-thread` variants, entry `web-build` of the todo store) uses this module with SDL3 built static (`ENGINE_STATIC_SDL`, the target `SDL3-shared` is an interface shim over `SDL3::SDL3-static`). It runs in headless Chromium and Edge (`tools/web/tests/web_smoke.py`, `tools/web/CLAUDE.md`); what stays unchecked is listed at the end.

- **Threads.** The default web build is threaded: `cmake/BagiEngine.cmake` sets `SDL_PTHREADS=ON` (SDL's own default for Emscripten is OFF) and `-pthread` reaches SDL and SDL_image through `CMAKE_C_FLAGS`; the single-thread presets set `SDL_PTHREADS=OFF`. The `TaskManager`'s workers are the pool of Web Workers (`TaskSystem/CLAUDE.md`, "Browser"). A threaded page needs `SharedArrayBuffer`, so it must be cross-origin isolated: `serve.py` sends the two headers and the shell refuses to start without `crossOriginIsolated` (`tools/web/CLAUDE.md`, "The page and the isolation").
- **Renderer.** `SDLRendererBackend` on SDL's `opengles2` driver over WebGL; there is no Vulkan. The renderer asks for vsync in a browser: without it SDL's driver runs the main loop on `setTimeout(0)` instead of `requestAnimationFrame`. The overlay `web/config/` drops `SDL_WINDOW_VULKAN` and asks for `SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`.
- **The page.** `web/shell.html` (`--shell-file`) has the `<canvas id="canvas">`; SDL looks for `#canvas`. The canvas is `100vw` x `100vh` by CSS (`100dvh` where known).
- **Canvas size.** `SDLMainWindow::Initialize` adds `SDL_WINDOW_OPENGL` (else the GLES2 renderer recreates the window, which drops every page handler of these events, ours too), then calls `InstallWebCanvasResize(window)`. It sets the window to the inner size of the page (`FitWindowSize`: rounded, at least 64 units) at once and on every resize of the page; `SDL_SetWindowSize` then sizes the canvas; `SDL_WINDOW_HIGH_PIXEL_DENSITY` makes the backing store `devicePixelRatio` times the window (`GetPixelDensity()`).
- **Fullscreen of the page.** The shell asks for fullscreen on `document.documentElement` (`tools/web/CLAUDE.md`), not the canvas and not `SDL_SetWindowFullscreen`. SDL's `fullscreenchange` handler sends `SDL_EVENT_WINDOW_RESIZED` with the desktop-mode size without resizing the canvas; ours is registered after it and puts the page size back. `FitWindowToPage` skips only when `document.fullscreenElement` is a `CANVAS` (SDL's own fullscreen).
- **Safe area.** SDL's driver answers the whole window. `InstallWebCanvasResize` reads `env(safe-area-inset-*)` through the padding of a hidden fixed element (`window.bagiSafeAreaProbe`; the shell's `viewport-fit=cover` makes the insets non-zero) at start, on resize, `fullscreenchange` and `orientationchange`, and `GetSafeArea()` answers from the kept values through `SafeAreaFromInsets` in window units with no DOM access per call. A change pushes `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` (one resize can give two; no payload).
- **Input.** SDL sends finger events, which become pointers of their own like on a phone; the mouse events it synthesizes from them are dropped (`IsDuplicateOfFinger`), so a tap reaches the game once. The canvas has `touch-action: none`.
- **Clipboard.** SDL has none on the web: `SDLClipboard.cpp` has the `EM_ASM` glue over `navigator.clipboard` and the `paste` event (`BECore/Clipboard/CLAUDE.md`, "Browser").
- **Posting data.** `SDLWebPost` calls `window.bagiWebPost` (`web/shell.html`, a script before the module): `start` returns an id (0 = refused: path, size, or 64 posts in flight), `poll` -1 pending, 0 failed, else the HTTP status once; polled, so no exported C function. Main thread only. The body is read by `TextDecoder` over `HEAPU8.slice` (a view of shared memory is refused); `keepalive` only up to 60000 bytes. Server end: `tools/web/CLAUDE.md`.
- **Tests.** `FitWindowSize` and `SafeAreaFromInsets` run on the desktop (`WebCanvasTest`). EM_ASM code is stringized as written: keep it between `// clang-format off/on` and put no comma outside parentheses (the macro splits its arguments there).
- **Checked in a browser** (headless Chromium and Edge, emulated touch and pixel ratio): the canvas fills the window and follows `set_viewport_size` with a backing store of `devicePixelRatio` times that, and the glue and SDL's resize handler do not fight over the size. **Not checked:** the fullscreen, safe area, clipboard and posting glue (compiled, not run), a real phone, a real GPU, Firefox and Safari, the loading bar over a real network.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame: (1) `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`; (2) `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and `ImGui_ImplSDLRenderer3_NewFrame()`; (3) `ImGuiWidget` calls `ImGui::NewFrame()` and the widgets build their windows; (4) `ImGuiWidget` calls `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`, and `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events, so widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures by logging/assertions. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) to a silent failure. Resolve asset paths through the `FileSystem`.
