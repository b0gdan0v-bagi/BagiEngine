# SDL Module Guidelines

## Overview

SDL3 integration: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement `BECore/Renderer` (`.claude/rules/rendering-module.md`: texture pipeline, serialization, fallback).

On the web (Emscripten) there is no Vulkan: the window config and the renderer come from the overlay `web/config/` (`ApplicationConfig.xml` with `SDLRendererBackend`, `SDLWindowConfig.xml` without `SDL_WINDOW_VULKAN`), the last content root of `src/main.cpp` (`BAGI_WEB_ROOT`), so it wins over the game's files of the same name. A renderer name that no backend registers (`VulkanRendererBackend` in a build without `ENGINE_WITH_VULKAN`) is a logged error of `SDL3ApplicationFabric::Create`. Details, the canvas glue and tests: "Web build" below.

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: takes the reflected `ApplicationConfig` (`Create(const ApplicationConfig&, const ApplicationOptions&)`) and creates the window from the config file named by `path` (an empty `path` is a logged error), the renderer named by `renderer` (default `SDLRendererBackend`, also when `renderer` is empty or the run is headless) and the events provider |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate. `SetHeadless(true)` before `Initialize()` makes it a headless window, which answers size, density and safe area from its simulation (see "Headless and screenshots"). A real window's `GetWidth()` / `GetHeight()` read the live size in window units (the config's numbers only while there is no window or SDL cannot tell, 0 after `Destroy()`); the density, display scale and safe area read the live window and fall back to the neutral values of `IMainWindow` when SDL fails (on the web the safe area is the insets of the page); see "Input and window metrics" |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends. `GetOutputWidth/Height()` is `SDL_GetCurrentRenderOutputSize`; the draws apply the view transform and blend with `GetBlendMode()` (`ToSDLBlendMode()`, set on every draw); `DrawTransformedTexture()` is `SDL_RenderTextureRotated` (identity: a plain `DrawTexture`; `.claude/rules/rendering-module.md`); `DrawTexture` modulates the texture by the tint on every draw; the clip stack goes to `SDL_SetRenderClipRect` in `OnClipRectChanged()` (grown to whole pixels); `CreateTexture()` is a static `RGBA32` texture; `DrawGeometry()` is `SDL_RenderGeometryRaw` over scratch arrays kept between calls (view transform and translation applied on the CPU); SDL only queues it: the software renderer rasterizes at the present (the cost shows in `SDLRenderer::PresentFrame`; `.claude/rules/ui-shapes.md`); `GetGeometryDetail()` is `Reduced` on the software renderer, `Full` on a GPU driver; `RequestCapture()` reads the pixels back at the present; batching is on ("Batching" below) |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events; see "Input and window metrics" |
| `SDLScreenshot` | `EncodePng()` (scale down, PNG in memory) and `RegisterScreenshotControl()`: the `screenshot` method of the control API |
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

**Window title.** `Application` calls `IMainWindow::SetTitle()` with `ComposeWindowTitle` (`BECore/MainWindow/WindowTitle.h`, pure) after `GameManager::Initialize()` and when the game's name or the run mode changes: editor `<Game> — BagiEngine Editor`; game mode the `title` of a game's own `SDLWindowConfig.xml`, else the game's name, else `BagiEngine`.

## SDL3 vs SDL2 Differences

- Event types use the `SDL_EVENT_` prefix; rectangles are `SDL_FRect` (float); `SDL_Init()` returns `bool`, not `int`.

## Best Practices

- Forward declare SDL types in headers (`struct SDL_Window;`); use accessors, no raw SDL pointers kept long-term; check return values (SDL3 functions may fail).
- Do not poll SDL events outside `SDLEventsProvider`. Engine-facing code depends on `IRenderer` / `ITexture`, never on raw SDL handles.

## Headless and screenshots

`--headless` (`ApplicationOptions::headless`, set by `Application` from `ControlSettings`; `Control/CLAUDE.md`) runs a game with no window at all:

- `SDLMainWindow` sets the hint `SDL_HINT_VIDEO_DRIVER` to `dummy` before `SDL_Init` (reset on `Destroy()`) and takes `SDL_WINDOW_VULKAN` out of the configured flags. The window has the configured size, or the one of `--window` / `--density` / `set_window` (`.claude/rules/control-window-api.md`); the picture is its size times the density.
- `SDL3ApplicationFabric` ignores the `renderer` attribute and creates `SDLRendererBackend`, which asks SDL for the `software` renderer when its window is headless (the Vulkan backend needs a real surface). The texture loader follows the renderer (`SDLTextureLoader::CanLoad`), so sprites load as usual.
- The `dummy` driver with the software renderer works on macOS and Windows 11; a hidden `cocoa` window does not (`software not available`), and `offscreen` needs OpenGL.
- The Editor run mode cannot be headless (its viewport is an offscreen render target, which this backend does not implement): `Application` stops the start-up with a log line.
- A headless game has no vsync: on the real clock it draws as fast as it can, so use `--manual-clock`.

`IRenderer::RequestCapture(callback)` is how pixels leave a renderer: `SDLRendererBackend` serves the request in `PresentFrame()` just before `SDL_RenderPresent` (`SDL_RenderReadPixels`, converted to RGBA8; after the present the back buffer is undefined) and hands a `PixelBuffer` to the callback. A newer request replaces an unserved one, whose callback gets an error. The headless run and the windowed SDL renderer support it; `VulkanRendererBackend` does not (the default of `IRenderer` answers with an error).

The `screenshot` method (`RegisterScreenshotControl`, registered by `SDL3ApplicationFabric::Create`) asks for the capture and answers one frame of zero seconds later: on the manual clock a frame of its own, on the real clock the frame being run. It answers a reflected `ScreenshotAnswer` `{height, png_base64, width}` (the base64 string is moved in); `maxSize` (16 to 4096, default 1024) cuts the longer side, the proportions are kept and a picture is never enlarged. Tests: `tests/BECore/SDLCaptureTest.cpp`.

## Batching

`SDLRendererBackend` turns the `IRenderer` batcher on: `SubmitBatch()` is one `SDL_RenderGeometryRaw` over the scratch arrays (`QueueGeometry`, shared with `DrawGeometry`; the vertices are already in output pixels with the opacity applied, the blend mode is the batch's own, and a geometry draw ignores the texture's modulation, so a texture left tinted by an immediate draw does not tint a batch). Every immediate draw, `Clear`, the render-clear event, the present (before the capture reads the pixels), the ImGui render, `EndFrame` and the target calls `Flush()` first; `Destroy()` drops the open batch. A GPU driver batches everything (a quad is two triangles on every path).

**The software renderer batches only what `SDLBatchCaptureTest` proved byte-equal** (`SetBatchOnlyExactDraws`, true by default): a rect on whole output pixels with positive size that is opaque, additive or multiplied, and `DrawTransformedRect()` (geometry in both forms). Drawn at once, because the images differ: a fractional edge in any mode, a translucent straight-alpha rect (the triangle blend rounds differently from `SDL_BlendFillRects`), a rect without extent, and **every textured quad** (the texel shrink of "Software rasterizer", `.claude/rules/rendering-module.md`), so the headless `stress-sprites` stays unbatched. `SetBatchOnlyExactDraws(false)` batches everything there, for the test that compares images; prove a new class with `DifferingPixels()` before `BatchesRect()` lets it through.

## The software present allocates per rotated sprite

`SDL_RenderPresent` of the software renderer makes **5 allocations per rotated sprite per frame** (all in `SDLRenderer::PresentFrame`; `SceneDrawSystem::Run` allocates nothing in a steady frame): `DrawTransformedTexture` is `SDL_RenderTextureRotated`, SDL queues `COPY_EX` (`SW_RenderCopyEx`), which clones the source surface, scales or crops it into a second one and rotates it into a third. A sprite with no rotation and no flip is a plain blit with none (read in the source, not measured). The GPU drivers queue a rotated quad with no CPU surface. **Nothing cheap on the engine's side:** a quarter turn is a transpose, which no flip does, a half turn is two flips, which `COPY_EX` allocates for as well, and `DrawGeometry` is wrong for a turned texture on this renderer (`.claude/rules/rendering-module.md`, "Software rasterizer"). The code stays as it is.

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

Mouse positions are in output pixels, not scene units (`Scene::ScreenToSceneX/Y` maps them): SDL reports them in window units, and the provider multiplies the position of a button, motion (`x`, `y`, `xrel`, `yrel`) and wheel event (`mouse_x`, `mouse_y`; the scroll amount stays) by `IMainWindow::GetPixelDensity()` (1 with no window) before `UIInput` sees it, read per event because a window can move to another display. The raw `SDLEventWrapper` that ImGui reads stays in window units (ImGui scales itself), and so does the viewport-local position that `ViewportWidget` re-emits in the editor.

**`UIInput`** (`BECore/Input/UIInput.h`) is the one door for mouse input: the provider and `ViewportWidget` call `DispatchMouseButtonDown/Up`, `DispatchMouseMotion`, `DispatchMouseWheel` and only an event nobody consumed is emitted for game code. The order, the shapes, the capture and the reentrancy rules: `BECore/Input/CLAUDE.md`.

**While the game is paused** (`CoreManager::GetPause().IsPaused()`, which includes the manual clock) the provider drops the game-bound branches: mouse (so `UIInput` sees nothing), key and text. They are dropped, not queued: a click made in the pause does not fire on resume. Kept: the raw `SDLEventWrapper` (ImGui, `Ctrl+Shift+E` and `Ctrl+Shift+P` read it), `QuitEvent` and the window, safe-area and orientation events (a resize re-lays the scene). `UIInput` itself is not gated: input injected by the control API (`ui_click`, `click_at`) works in a pause. `ViewportWidget::ForwardMouse` does the same and forgets a press that began before the pause; a canvas that took a press before it keeps it until the next click.

`ViewportWidget` (Editor mode) feeds the same door with viewport-local positions: down only while the image is hovered, up and motion also after the pointer leaves it (when the press began there), wheel while hovered.

**Window metrics** (`IMainWindow`): `GetWidth()` / `GetHeight()` and `GetSafeArea()` are in window units, `GetPixelDensity()` is output pixels per window unit (2 on a Retina display: the shipped window config asks for the high-density mode, and both renderer backends follow the window's pixel size), `GetDisplayScale()` is the user's display scale. Layout takes all three as inputs. The dummy driver has no density and no notch, so a headless `SDLMainWindow` answers them from its simulation (1, 1 and the whole window until set).

Tests: `tests/BECore/SDLEventsProviderTest.cpp` (conversions, the capture and pause rules, with the real SDL queue), `UIInputTest.cpp`, `WindowMetricsTest.cpp`.

## Web build

The browser build (`ENGINE_TARGET_WEB`, presets `web-release` / `web-debug` and their `-single-thread` variants, entry `web-build` of the todo store) uses this module with SDL3 built static (`ENGINE_STATIC_SDL`, the target `SDL3-shared` is an interface shim over `SDL3::SDL3-static`). It builds with emsdk 4.0.23 (SDL3 and SDL_image static from the vendored sources) and runs in headless Chromium and Edge (`tools/web/tests/web_smoke.py`, `tools/web/CLAUDE.md`); what stays unchecked is listed at the end.

- **Threads.** The default web build is threaded: `cmake/BagiEngine.cmake` sets `SDL_PTHREADS=ON` (SDL's own default for Emscripten is OFF) and `-pthread` reaches SDL and SDL_image through `CMAKE_C_FLAGS`; the single-thread presets set it OFF. The `TaskManager`'s workers are Web Workers (`TaskSystem/CLAUDE.md`, "Browser"). A threaded page must be cross-origin isolated (`SharedArrayBuffer`): `serve.py` sends the COOP and COEP headers and the shell refuses to start otherwise (`tools/web/CLAUDE.md`, "The page and the isolation").
- **Renderer.** `SDLRendererBackend` on SDL's `opengles2` driver over WebGL; there is no Vulkan. The renderer asks for vsync in a browser (`#ifdef __EMSCRIPTEN__`): without it SDL's Emscripten driver runs the main loop on `setTimeout(0)` instead of `requestAnimationFrame`. The overlay `web/config/` drops `SDL_WINDOW_VULKAN` and asks for `SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`.
- **The page.** `web/shell.html` (`--shell-file`) has the `<canvas id="canvas">`; SDL looks for `#canvas` (the selector hint is at its default). The canvas is `100vw` x `100vh` by CSS. Loading bar and messages: `tools/web/CLAUDE.md`.
- **Canvas size.** `SDLMainWindow::Initialize` adds `SDL_WINDOW_OPENGL` (else the GLES2 renderer recreates the window and drops the page handlers), then calls `InstallWebCanvasResize(window)`. It sets the window to the inner size of the page (`FitWindowSize`: rounded, at least 64 units) at once and on every resize of the page; `SDL_SetWindowSize` then sizes the canvas. `SDL_WINDOW_HIGH_PIXEL_DENSITY` makes the backing store `devicePixelRatio` times the window, which `GetPixelDensity()` reports.
- **Fullscreen of the page.** The shell asks for fullscreen on `document.documentElement` (`tools/web/CLAUDE.md`), not the canvas and not `SDL_SetWindowFullscreen`. SDL's `fullscreenchange` handler reports the desktop-mode size without resizing the canvas; ours is registered after it and puts the page size back (skipped only when the fullscreen element is a `CANVAS`). `orientationchange` fits too.
- **Safe area.** SDL's driver answers the whole window. `InstallWebCanvasResize` reads `env(safe-area-inset-*)` through the padding of a hidden fixed element (`window.bagiSafeAreaProbe`; the shell's `viewport-fit=cover` makes the insets non-zero) at start, on resize, `fullscreenchange` and `orientationchange`; `GetSafeArea()` answers from the kept values with no DOM access per call. A change pushes `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` (one resize can give two).
- **Input.** SDL sends finger events, which become pointers of their own like on a phone; the mouse events it synthesizes from them are dropped (`IsDuplicateOfFinger`), so a tap reaches the game once. The canvas has `touch-action: none`.
- **Clipboard.** SDL has none on the web: `SDLClipboard.cpp` has the `EM_ASM` glue over `navigator.clipboard` and the `paste` event (`BECore/Clipboard/CLAUDE.md`, "Browser").
- **Posting data.** `SDLWebPost` calls `window.bagiWebPost` (`web/shell.html`): `start` returns an id (0 = refused), `poll` -1 pending, 0 failed, else the HTTP status once; polled, so no exported C function; main thread only. The body is read from a `HEAPU8.slice` (a view of shared memory is refused). Limits in `SDLWebPost.h`; server end: `serve.py` (`tools/web/CLAUDE.md`).
- **Tests.** `FitWindowSize` and `SafeAreaFromInsets` run on the desktop (`WebCanvasTest`). EM_ASM code is stringized as written: keep it between `// clang-format off/on`, no comma outside parentheses.
- **Checked in a browser** (headless Chromium and Edge, emulated touch and pixel ratio): the canvas fills the window and follows `set_viewport_size` at `devicePixelRatio` times that, and the glue and SDL's resize handler do not fight. **Not checked:** the fullscreen, safe area, clipboard and posting glue (compiled, not run), a real phone, a real GPU, Firefox and Safari, the loading bar over a real network.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame: (1) `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`; (2) `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and `ImGui_ImplSDLRenderer3_NewFrame()`; (3) `ImGuiWidget` calls `ImGui::NewFrame()` and the widgets build their windows; (4) `ImGuiWidget` calls `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`, and `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events, so widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures by logging/assertions. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) to a silent failure. Resolve asset paths through the `FileSystem`.
