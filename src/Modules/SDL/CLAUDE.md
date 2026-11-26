# SDL Module Guidelines

## Overview

SDL3: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement `BECore/Renderer` (`.claude/rules/rendering-module.md`).

On the web (Emscripten) there is no Vulkan: the window config and the renderer come from the overlay `web/config/` (`ApplicationConfig.xml` with `SDLRendererBackend`, `SDLWindowConfig.xml` without `SDL_WINDOW_VULKAN`), the last content root of `src/main.cpp` (`BAGI_WEB_ROOT`), so it wins over the game's files of the same name. A renderer name that no backend registers (`VulkanRendererBackend` in a build without `ENGINE_WITH_VULKAN`) is a logged error of `SDL3ApplicationFabric::Create`. Details: "Web build".

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: takes the reflected `ApplicationConfig` (`Create(const ApplicationConfig&, const ApplicationOptions&)`) and creates the window from the config file named by `path` (an empty `path` is a logged error), the renderer named by `renderer` (default `SDLRendererBackend`, also when `renderer` is empty or the run is headless) and the events provider; `options.backend` overrides the renderer name (see "Headless and screenshots") |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate. `SetHeadless(true)` before `Initialize()` makes it a headless window, which answers size, density and safe area from its simulation (see "Headless and screenshots"). A real window's size, density, display scale and safe area are read live and fall back to the config's size or the neutral values of `IMainWindow` when SDL cannot tell (0 size after `Destroy()`; on the web the safe area is the insets of the page); see "Input and window metrics" |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends. `GetOutputWidth/Height()` is `SDL_GetCurrentRenderOutputSize`; the draws apply the view transform and blend with `GetBlendMode()` (`ToSDLBlendMode()`); `DrawTransformedTexture()` is `SDL_RenderTextureRotated`; `DrawTexture` modulates the texture by the tint on every draw; the clip stack goes to `SDL_SetRenderClipRect` (grown to whole pixels); `CreateTexture()` is a static `RGBA32` texture; `DrawGeometry()` is `SDL_RenderGeometryRaw` over scratch arrays kept between calls (view transform and translation applied on the CPU); SDL only queues it, the software renderer rasterizes at the present (`.claude/rules/ui-shapes.md`); `GetGeometryDetail()` is `Reduced` on the software renderer; `RequestCapture()` reads the pixels back at the present; batching: "Batching"; `CreateRenderTarget()`: "Render targets" |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events; see "Input and window metrics" |
| `SDLScreenshot` | `EncodePng()` (scale down, PNG in memory) and `RegisterScreenshotControl()`: the `screenshot` method of the control API |
| `SDLRenderTarget` | `IRenderTarget` over a `SDL_TEXTUREACCESS_TARGET` texture (the ImGui texture id is the `SDL_Texture*`); the renderer lists the live ones and releases their texture in `Destroy()`, so a target may outlive it |
| `SDLTexture` / `SDLTextureLoader` | `ITexture` implementation and its `IResourceLoader` |
| `ImageFile` | `ReadImageFile()`: straight RGBA for the texture loaders and the editor. A PNG goes through `stb_image`: SDL_image on Apple uses ImageIO, which premultiplies the alpha |
| `SDLWindowConfig` | Window settings (`config/SDLWindowConfig.xml`: size, `windowFlags`, an optional `title`; the shipped file asks for `SDL_WINDOW_HIGH_PIXEL_DENSITY`) |
| `SDLWebCanvas` | `FitWindowSize()` and `SafeAreaFromInsets()` (pure, every platform: the window size for a browser window; the safe area from the four CSS insets) and, Emscripten only, `InstallWebCanvasResize()` and `GetWebSafeArea()`; see "Web build" |
| `SDLAudioBackend` | `BECore/Audio/CLAUDE.md` |
| `SDLClipboard` | `SDLClipboardBackend` (`SDL_SetClipboardText` / `SDL_GetClipboardText`, installed by `SDL3ApplicationFabric::Create`) and the pure `IsPasteShortcut`. The service and its rules: `BECore/Clipboard/CLAUDE.md` |
| `SDLWebPost` | `WebPostStart()` / `WebPostPoll()` (text POST to a path of the page's own origin, answered by polling), `IsWebPostPathValid()` and the limits: pure, every platform; the browser half is Emscripten only, every other build says "unavailable"; see "Web build" |
| `SDLWebDownload` | `WebDownloadStart()` (a Blob of a `HEAPU8.slice` copy and a clicked temporary link) and `IsWebDownloadNameValid()`; split as `SDLWebPost` |
| `SDLUtils` | Shared SDL helpers |

`IMainWindow`, `MainWindowManager` and `MainWindowAccessor` live in `BECore/MainWindow/`, not here.

**Window title.** `Application` calls `IMainWindow::SetTitle()` with `ComposeWindowTitle` (`BECore/MainWindow/WindowTitle.h`, pure) after `GameManager::Initialize()` and when the game's name or the run mode changes: editor `<Game> — BagiEngine Editor`; game mode the `title` of a game's own `SDLWindowConfig.xml`, else the game's name, else `BagiEngine`.

## SDL3 vs SDL2 Differences

- Event types use the `SDL_EVENT_` prefix; rectangles are `SDL_FRect` (float); `SDL_Init()` returns `bool`, not `int`.

## Best Practices

- Forward declare SDL types in headers (`struct SDL_Window;`); no raw SDL pointers kept long-term; check return values (SDL3 functions may fail).
- Do not poll SDL events outside `SDLEventsProvider`; engine-facing code uses `IRenderer` / `ITexture`, never raw SDL handles.

## Headless and screenshots

`--headless` (`ApplicationOptions::headless`, set by `Application` from `ControlSettings`; `Control/CLAUDE.md`) runs a game with no window at all:

- `SDLMainWindow` sets the hint `SDL_HINT_VIDEO_DRIVER` to `dummy` before `SDL_Init` (reset on `Destroy()`) and takes `SDL_WINDOW_VULKAN` out of the flags. The window has the configured size, or the one of `--window` / `--density` / `set_window` (`.claude/rules/control-window-api.md`).
- `SDL3ApplicationFabric` ignores the `renderer` attribute and creates `SDLRendererBackend`, which asks SDL for the `software` renderer when its window is headless (the Vulkan backend needs a real surface). The texture loader follows the renderer (`SDLTextureLoader::CanLoad`).
- The `dummy` driver with the software renderer works on macOS and Windows 11, and the Editor run mode too (`--headless --editor`: ImGui and the viewport target run on it); a hidden `cocoa` window with it does not (`software not available`), and `offscreen` needs OpenGL.
- A headless game has no vsync: on the real clock it draws as fast as it can, so use `--manual-clock`.

**`--hidden-window`** is the other run with no window on the screen, with a real GPU: a borderless, unfocusable utility window that `SDLMainWindow` creates hidden, moves to a position no display covers and shows. Left hidden it is paced at the display's refresh rate (macOS, Metal and MoltenVK) whatever the present mode, so an "uncapped" benchmark would measure vsync. It drops `SDL_WINDOW_MAXIMIZED` and `SDL_WINDOW_HIGH_PIXEL_DENSITY` so the output is the config's size in pixels on every display (`SDLUtils::ApplyStartOptions`, pure), and the fabric calls `IRenderer::SetUncapped(true)`: SDL's renderer turns vsync off. `--backend=` picks the renderer: `sdl-gpu` is `SDLRendererBackend` without `SDL_WINDOW_VULKAN` (it refuses a software renderer rather than benchmark it), `vulkan` adds the flag and creates `VulkanRendererBackend`, `software` is the headless one. A backend that was asked for and cannot start logs `Backend '<name>' is not available on this machine: <reason>` and the start-up fails (`tools/perf/stress_frames.py` greps that text to skip).

`IRenderer::RequestCapture(callback)` is how pixels leave a renderer: `SDLRendererBackend` serves the request in `PresentFrame()` just before `SDL_RenderPresent` (`SDL_RenderReadPixels`, as RGBA8; the back buffer is undefined after it) and hands a `PixelBuffer` to the callback. A newer request replaces an unserved one, whose callback gets an error. The headless run and the windowed SDL renderer support it; `VulkanRendererBackend` does not (the default of `IRenderer` answers with an error).

The `screenshot` method (`RegisterScreenshotControl`, registered by `SDL3ApplicationFabric::Create`) asks for the capture and answers one frame of zero seconds later: on the manual clock a frame of its own, on the real clock the frame being run. It answers a reflected `ScreenshotAnswer` `{height, png_base64, width}` (the base64 string is moved in); `maxSize` (16 to 4096, default 1024) cuts the longer side, the proportions are kept and a picture is never enlarged.

## Batching

`SDLRendererBackend` turns the `IRenderer` batcher on (`.claude/rules/rendering-module.md`, "Batching"): `SubmitBatch()` is one `SDL_RenderGeometryRaw` over the scratch arrays shared with `DrawGeometry` (vertices already in output pixels with the opacity applied, the blend mode is the batch's own; a geometry draw ignores the texture's modulation, so a texture left tinted by an immediate draw does not tint a batch). Every immediate draw, `Clear`, the present (before the capture reads the pixels), the ImGui render, `EndFrame` and the target calls `Flush()` first; `Destroy()` drops the open batch.

**The software renderer batches only what `SDLBatchCaptureTest` proved byte-equal** (`SetBatchOnlyExactDraws`, true by default): a whole-pixel rect with positive size that is opaque, additive or multiplied, and `DrawTransformedRect()`. Drawn at once, because the images differ: a fractional edge, a translucent straight-alpha rect (the triangle blend rounds differently from `SDL_BlendFillRects`), a rect without extent and every textured quad (the texel shrink of "Software rasterizer"), so the headless `stress-sprites` stays unbatched. `SetBatchOnlyExactDraws(false)` batches everything there, for the test that compares images; prove a new class with `DifferingPixels()` before `BatchesRect()` lets it through.

## Render targets

`SetRenderTarget(target)` flushes the open batch (it belongs to the target it was drawn for; `UnsetRenderTarget()` does the same), then `SDL_SetRenderTarget` and a clear with the colour of the last `Clear()` (then opaque black, as Vulkan). `BeginFrame(color)` fills the back buffer; until the present `Clear()` only names the colour of the next target (outside a frame it fills the back buffer). A target of another backend or one whose renderer is gone keeps the back buffer (`LOG_ERROR`). `PresentFrame()` unsets a target left open first: the window is what is presented and what a capture reads, so an editor screenshot is the whole window. SDL keeps the clip per target.

## The software present allocates per rotated sprite

`SDL_RenderPresent` of the software renderer makes **5 allocations per rotated sprite per frame** (all in `SDLRenderer::PresentFrame`; `SceneDrawSystem::Run` allocates nothing): `COPY_EX` (`SW_RenderCopyEx`, queued by `SDL_RenderTextureRotated`) clones the source surface, scales or crops it into a second one and rotates it into a third; a sprite with no rotation and no flip is a plain blit. **Nothing cheap on the engine's side:** a quarter turn is a transpose no flip does, a half turn is two flips `COPY_EX` allocates for as well, and `DrawGeometry` is wrong for a turned texture here (`.claude/rules/rendering-module.md`, "Software rasterizer").

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

Mouse positions are in output pixels, not scene units (`Scene::ScreenToSceneX/Y` maps them): SDL reports window units, and the provider multiplies the position of a button, motion (`x`, `y`, `xrel`, `yrel`) and wheel event (`mouse_x`, `mouse_y`; the scroll amount stays) by `IMainWindow::GetPixelDensity()` (1 with no window) before `UIInput` sees it, read per event because a window can move to another display. The raw `SDLEventWrapper` that ImGui reads stays in window units (ImGui scales itself), and so does the viewport-local position of `ViewportWidget` in the editor.

**`UIInput`** (`BECore/Input/UIInput.h`) is the one door for mouse input: the provider and `ViewportWidget` call `DispatchMouseButtonDown/Up`, `DispatchMouseMotion`, `DispatchMouseWheel` and only an event nobody consumed is emitted for game code. The order, the shapes, the capture and the reentrancy rules: `BECore/Input/CLAUDE.md`.

**While the game is paused** (`CoreManager::GetPause().IsPaused()`, the manual clock included) the provider drops the game-bound branches: mouse (so `UIInput` sees nothing), key and text; dropped, not queued, so a click made in the pause does not fire on resume. Kept: the raw `SDLEventWrapper` (ImGui, `Ctrl+Shift+E` and `Ctrl+Shift+P` read it), `QuitEvent` and the window, safe-area and orientation events. `UIInput` itself is not gated: input injected by the control API (`ui_click`, `click_at`) works in a pause. `ViewportWidget::ForwardMouse` does the same and forgets a press that began before the pause.

`ViewportWidget` (Editor mode) feeds the same door with viewport-local positions: down only while hovered, up and motion also after the pointer leaves (when the press began there), wheel while hovered.

**Window metrics** (`IMainWindow`): `GetWidth()` / `GetHeight()` and `GetSafeArea()` are in window units, `GetPixelDensity()` is output pixels per window unit (2 on a Retina display: the shipped window config asks for the high-density mode, and both renderer backends follow the window's pixel size), `GetDisplayScale()` is the user's display scale. The dummy driver has no density and no notch, so a headless `SDLMainWindow` answers them from its simulation (1, 1 and the whole window until set).

## Web build

The browser build (`ENGINE_TARGET_WEB`, presets `web-release` / `web-debug` and their `-single-thread` variants) uses this module with SDL3 built static (`ENGINE_STATIC_SDL`, the target `SDL3-shared` is an interface shim over `SDL3::SDL3-static`). It builds with emsdk 4.0.23 and runs in headless Chromium and Edge (`tools/web/tests/web_smoke.py`, `tools/web/CLAUDE.md`).

- **Threads.** The default web build is threaded: `cmake/BagiEngine.cmake` sets `SDL_PTHREADS=ON` (SDL's default for Emscripten is OFF) and `-pthread` reaches SDL and SDL_image through `CMAKE_C_FLAGS`; the single-thread presets set it OFF. The `TaskManager`'s workers are Web Workers (`TaskSystem/CLAUDE.md`, "Browser"). A threaded page must be cross-origin isolated (`SharedArrayBuffer`): `serve.py` sends the COOP and COEP headers (`tools/web/CLAUDE.md`, "The page and the isolation").
- **Renderer.** `SDLRendererBackend` on SDL's `opengles2` driver over WebGL; there is no Vulkan. The renderer asks for vsync in a browser (`#ifdef __EMSCRIPTEN__`): without it the main loop runs on `setTimeout(0)` instead of `requestAnimationFrame`. The overlay `web/config/` drops `SDL_WINDOW_VULKAN` and asks for `SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`.
- **The page.** `web/shell.html` (`--shell-file`) has the `<canvas id="canvas">`; SDL looks for `#canvas` (the selector hint is at its default). The canvas is `100vw` x `100vh` by CSS.
- **Canvas size.** `SDLMainWindow::Initialize` adds `SDL_WINDOW_OPENGL` (else the GLES2 renderer recreates the window and drops the page handlers), then calls `InstallWebCanvasResize(window)`, which sets the window to the inner size of the page (`FitWindowSize`: rounded, at least 64 units) at once and on every resize; `SDL_SetWindowSize` then sizes the canvas. `SDL_WINDOW_HIGH_PIXEL_DENSITY` makes the backing store `devicePixelRatio` times the window, which `GetPixelDensity()` reports.
- **Fullscreen of the page.** The shell asks for fullscreen on `document.documentElement` (`tools/web/CLAUDE.md`), not the canvas and not `SDL_SetWindowFullscreen`. SDL's `fullscreenchange` handler reports the desktop-mode size without resizing the canvas; ours runs after it and puts the page size back (not when the fullscreen element is a `CANVAS`); `orientationchange` fits too.
- **Safe area.** SDL's driver answers the whole window. `InstallWebCanvasResize` reads `env(safe-area-inset-*)` through the padding of a hidden fixed element (`window.bagiSafeAreaProbe`; the shell's `viewport-fit=cover` makes the insets non-zero) at start and on resize, `fullscreenchange` and `orientationchange`; `GetSafeArea()` answers from the kept values. A change pushes `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED`.
- **Input.** SDL sends finger events, which become pointers of their own like on a phone; the mouse events it synthesizes from them are dropped (`IsDuplicateOfFinger`). The canvas has `touch-action: none`.
- **Clipboard.** SDL has none on the web: `SDLClipboard.cpp` has `EM_ASM` glue over `navigator.clipboard` and the `paste` event (`BECore/Clipboard/CLAUDE.md`, "Browser").
- **Posting data.** `SDLWebPost` calls `window.bagiWebPost` (`web/shell.html`): `start` returns an id (0 = refused), `poll` -1 pending, 0 failed, else the HTTP status once; main thread only, the body read from a `HEAPU8.slice` (a view of shared memory is refused). Limits: `SDLWebPost.h`; server end: `serve.py` (`tools/web/CLAUDE.md`).
- **Tests.** `FitWindowSize` and `SafeAreaFromInsets` run on the desktop (`WebCanvasTest`). EM_ASM code is stringized as written: keep it between `// clang-format off/on`.
- **Checked** in headless Chromium and Edge (emulated touch and pixel ratio): the canvas fills the window and follows `set_viewport_size`. **Not checked:** the fullscreen, safe area and posting glue (compiled, not run), a real phone, a real GPU, Firefox and Safari.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame: (1) `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`; (2) `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and `ImGui_ImplSDLRenderer3_NewFrame()`; (3) `ImGuiWidget` calls `ImGui::NewFrame()`, the widgets build their windows, then `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`, and `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events: widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures by logging. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) to a silent failure. Resolve asset paths through the `FileSystem`.
