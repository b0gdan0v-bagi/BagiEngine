# Vulkan Module Guidelines

## Overview

`VulkanModule` is the second renderer backend: `VulkanRendererBackend` implements `IRenderer` (see `.claude/rules/rendering-module.md` for the rules shared with SDL). It is selected by `renderer="VulkanRendererBackend"` in `config/ApplicationConfig.xml`; the window itself still comes from `SDLMainWindow` (`SDL_WINDOW_VULKAN` in `config/SDLWindowConfig.xml`). Links `BECoreModule`, `EventsModule`, `SDLModule` and `Vulkan::Headers`; every game executable links it with `WHOLE_ARCHIVE` (`be_add_game_executable`).

## Components (`src/CoreVulkan/`)

| Class | Role |
|-------|------|
| `VulkanRendererBackend` | `IRenderer`: frame lifecycle, command buffers, per-frame sync (`kMaxFramesInFlight = 2`), ImGui backend. `GetOutputWidth/Height()` is the extent of the open pass (swapchain or offscreen target), and the swapchain extent follows the window (rebuilt in `AcquireFrame()`, see "Frame lifecycle"); `DrawFilledRect` / `DrawTexture` apply `IRenderer::GetViewTransform()` to the rect on the CPU, so the shaders always get output pixels; `ResolveScissor()` turns the clip stack into the scissor of each draw (whole extent when unclipped, draw skipped when the clip is empty); `CreateTexture()` goes through `VulkanTextureUpload`; `WaitIdle()` is `vkDeviceWaitIdle` (a no-op once the device is destroyed) |
| `VulkanDevice` | Instance, GPU pick, logical device. Two-step init: `InitializeInstance()`, then `InitializeDevice(surface)` (the surface needs the instance). Memory-type lookup and single-time command helpers |
| `VulkanSwapchain` | Swapchain, image views, framebuffers and the render pass. `AcquireNextImage` / `Present` return a `SwapchainStatus` (`CORE_ENUM`: `Ok`, `Suboptimal`, `OutOfDate`, `Failed`, made by `ClassifyResult`). `Recreate(device, surface, pixelWidth, pixelHeight)` builds a new swapchain from the old one (`oldSwapchain`) and destroys the old objects only after the new ones exist; it returns a `SwapchainRecreateResult` (`CORE_ENUM`, `Done`, `ZeroExtent`, `Failed`): `ZeroExtent` (minimized) touches nothing and `Failed` is logged. The caller does `vkDeviceWaitIdle` first. The **render pass is created once and kept**: the rect, texture and geometry pipelines and ImGui's pipeline were built against it, so a recreated swapchain must have the same image format (`ChooseSurfaceFormat` prefers the current one); a surface that no longer offers it makes `Recreate` fail with a logged error. Sizes are pixels. `VulkanRendererBackend` calls it (below) |
| `VulkanSwapchainPolicy.h` | The decisions around the swapchain as pure, header-only functions: `ClassifyResult`, `ActionAfter(stage, status)` (what a frame does about the result of an acquire or a present), `IsMinimized`, `NeedsRecreate` (compares the window's pixel size with the size the swapchain was last *asked* for, not with the extent the surface gave back, so a surface with another fixed extent does not make it rebuild on every frame), `ChooseExtent`, `ChooseSurfaceFormat`, and the enums `SwapchainStatus`, `SwapchainStage`, `SwapchainAction` and `SwapchainRecreateResult` (`CORE_ENUM`). Needs `<vulkan/vulkan.h>` and BECore's `EnumUtils.h` (`BagiEngineTests` links BECore), so `VulkanSwapchainPolicyTest` runs without a device (the test target links `Vulkan::Headers` and takes the include path, not `VulkanModule`) |
| `VulkanRectPipeline`, `VulkanTexturePipeline` | Graphics pipelines for `DrawFilledRect` and `DrawTexture`. Both take the scissor of the draw as a parameter. The texture pipeline's push constants are 56 bytes: `dstRect`, `srcUV`, `tint` (vec4, multiplied with the texel in `texture.frag.glsl`), `screenSize`; the vertex and fragment shaders declare the same block |
| `VulkanGeometryPipeline`, `VulkanGeometryBuffer` | `DrawGeometry()`: a pipeline that reads a vertex and an index buffer (20-byte vertex: position in output pixels, `R8G8B8A8_UNORM` color, UV; `uint32` indices; push constant `screenSize`), and the memory it reads from. The pipeline uses the texture pipeline's descriptor set layout, so any `VulkanTexture` binds to it, and its fragment shader multiplies the texel by the vertex color. `VulkanGeometryBuffer` keeps a list of host-visible, coherent, persistently mapped buffers (1 MiB each, bigger for a bigger draw) **per frame in flight**: `Allocate()` hands out a piece, the backend rewinds the slot in `AcquireFrame()` right after it has waited on that slot's fence, and a full buffer is followed by a new one instead of being resized, so a recorded draw never points at memory that moved |
| `VulkanTextureUpload` | Pixels in memory to a `VulkanTexture` (staging buffer, device-local image, view, sampler, descriptor set, one single-time command buffer). Shared by `VulkanTextureLoader` (pixels decoded from a file) and `CreateTexture()`; main thread only |
| `VulkanTexture` | `ITexture`: image + memory + view + sampler + descriptor set (set 0). Destroy it only when the GPU is idle |
| `VulkanTextureLoader` | `IResourceLoader` for `.bmp .png .jpg .jpeg`; `CanLoad` is true **only while the active renderer is the Vulkan backend** (`SDLTextureLoader` does the mirror check). It decodes and converts the file, `VulkanTextureUpload` uploads. Pixels are uploaded as `SDL_PIXELFORMAT_RGBA32` (bytes R,G,B,A) to match `VK_FORMAT_R8G8B8A8_SRGB`; the packed `RGBA8888` would swap the channels on little-endian |
| `VulkanRenderTarget` | `IRenderTarget`: offscreen target; `GetImGuiTextureId()` is a `VkDescriptorSet` from `ImGui_ImplVulkan_AddTexture` |

## Frame lifecycle

Driven by events, not by direct calls:

1. `RenderEvents::NewFrameEvent` → `AcquireFrame()`: first it follows the window. It reads `SDL_GetWindowSizeInPixels` (the swapchain is made from that size too, not from `IMainWindow::GetWidth()`), and `NeedsRecreate(...)` compares it with the pixel size the swapchain was last created for; a `SDLEvents::WindowEvent` of type resized or pixel-size-changed, a suboptimal result and an out-of-date result mark the swapchain dirty as well, and the per-frame compare is the safety net for a driver (MoltenVK often answers `VK_SUCCESS` after a resize) that reports nothing. A rebuild is `vkDeviceWaitIdle`, `VulkanSwapchain::Recreate`, and `_currentExtent` follows the new extent; the pipelines and ImGui are untouched, since the render pass is kept. Then it acquires the image and opens the command buffer. This happens **before** the swapchain render pass so `ViewportWidget` can insert an offscreen pass first.
   - **Minimized** (a pixel size of zero): the frame is skipped, nothing is reset or recorded, and the thread sleeps `kMinimizedSleepMs` (16 ms) so that the loop does not spin.
   - **One place for the results:** `HandleSwapchainStatus(status, stage)` reads the result of every acquire and present through `ActionAfter`. An out-of-date acquire signaled nothing, so the swapchain is rebuilt at once and the acquire is tried once more in the same frame (if that fails too the frame is skipped and the swapchain stays dirty). A suboptimal acquire signaled its semaphore, so the frame goes on and is submitted, and the swapchain is rebuilt at the start of the next one. A present that is out of date or suboptimal only marks it dirty (the frame was submitted). Any other result is logged (an acquire that fails this way also skips the frame).
   - **A skipped frame records nothing:** `_frameAcquired` stays false, `BeginFrame()` / `EndFrame()` do nothing, every draw is dropped (they need an open pass), `SetRenderTarget()` returns without opening a pass (so a viewport draws nothing either, and `UnsetRenderTarget()` is a no-op), `OnImGuiRender()` records only inside the swapchain pass, and `Present()` presents and advances the frame slot only after a real submit.
   - **Fixed render pass:** the new swapchain keeps the image format of the old one. If the surface no longer offers that format (after a move to another display, say) `Recreate` fails with a logged error, the frame is skipped and the next frame tries again (after a pause of `kRecreateFailedSleepMs`); the render pass and the pipelines are not rebuilt.
2. `SetRenderTarget` / `UnsetRenderTarget` open and close an offscreen pass (the barrier on the target image is recorded in `UnsetRenderTarget`).
3. `RenderClearEvent` → begin the swapchain pass; widgets record their draw commands into it. In Game run mode there is no offscreen pass: `SceneRenderWidget` draws the scene into this pass in `DrawScreen()`.
4. `RenderPresentEvent` → end the pass, submit, present.

No test can create a Vulkan device, so the backend's frame code is not run by `ctest`; the decisions in it are the pure functions of `VulkanSwapchainPolicy.h` (`VulkanSwapchainPolicyTest`), and the behavior is checked by hand ("Checking by hand").

ImGui goes through `ImGuiInit/NewFrame/Render/ShutdownEvent` like the SDL backend (`imgui_impl_vulkan`, which is why the backend exposes `GetVk*` getters). `ApplicationCleanUpEvent` calls `Destroy()`.

## Checking by hand

The window cannot be driven by a test. Run `BagiMatch3` (or a game) with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (see "Looking at the pixels") and read stderr:

- Maximize and restore (the window starts `SDL_WINDOW_MAXIMIZED`): the picture refills the window with no stretch and no bars other than the scene's own.
- Drag-resize the window slowly and fast, both axes: no frozen or stretched frame, no validation errors, no crash on exit.
- Minimize and restore: no CPU spin while minimized (Activity Monitor), the picture comes back at the right size; also minimize while resizing.
- Move the window between a Retina and a non-Retina display (and change the display's scale in System Settings while the game runs): the output size follows.
- HiDPI (the window config asks for `SDL_WINDOW_HIGH_PIXEL_DENSITY`): on a Retina display the output (swapchain) is twice the window size in units and the picture is sharp, with no 2 px per scene unit blur; a click on a Match3 cell, a canvas button and, in a game with a view size, a tile hits the right target, also at the edges of the window; the UI keeps its size in window units.
- Editor mode (Ctrl+Shift+E, Debug build) on the Retina display: panels and fonts readable and not doubled, the viewport picture and its clicks as before (the viewport stays one pixel per ImGui unit, so it is as soft as today). ImGui's font sharpness there is not changed.
- Whether the "format no longer offered" case after a display move ever happens (the logged error of `Recreate`); if it does, the render pass and pipelines must be rebuilt, which is not done.

## No capture

`VulkanRendererBackend` does not implement `IRenderer::RequestCapture()`: the default answers with an error, and the `screenshot` method of the control API therefore answers `capture_failed` on this backend. A screenshot needs `--headless` (the SDL software renderer) until a Vulkan readback is written. The decision on the backends (the entry `renderer-backends` of the todo store) comes first.

## No blend modes

`IRenderer::SetBlendMode()` (`Alpha`, `Add`, `Multiply`) is not implemented here: `DrawFilledRect()`, `DrawTexture()` and `DrawGeometry()` draw with straight alpha whatever `GetBlendMode()` says, and `WarnIfBlendModeIgnored()` logs one warning per backend, naming the mode, the first time a draw arrives with another one. There is no pipeline, shader or descriptor for a blend mode. The rotated and flipped draws (`DrawTransformedRect()` / `DrawTransformedTexture()`) work, since they are `DrawGeometry()`. The decision on the backends (the entry `renderer-backends`: SDL is the main backend, Vulkan is frozen at its feature level) comes first; a blend pipeline per mode would be the way if this backend is ever brought up.

## Geometry

`VulkanRendererBackend::DrawGeometry()` (indexed triangles with a color and a UV per vertex, the primitive of the RmlUi host, `src/Modules/RmlUiHost/CLAUDE.md`) writes the vertices into the frame slot's `VulkanGeometryBuffer` with the translation and the view transform applied on the CPU, so the shader gets output pixels like the others, and draws with the scissor of the clip stack. An untextured draw binds a 1 x 1 white texture made on first use (`GetWhiteTextureDescriptorSet()`, freed in `Destroy()` before the texture pipeline's descriptor pool). An index past the vertices is logged and nothing is drawn, as in the SDL backend; a trailing index or two that do not make a triangle are dropped. Checked in a real window with the Khronos validation layer and the LunarG screenshot layer (see "Looking at the pixels").

## Looking at the pixels

The backend has no capture (above), but the Vulkan SDK's layers give one without code: `VK_LAYER_PATH=/usr/local/share/vulkan/explicit_layer.d VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation:VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=30 VK_SCREENSHOT_DIR=<dir>` before the game starts (a window, not `--headless`; start it with `--control --manual-clock` and `step` 40 frames so the frame exists, then `quit`). The layer writes `<frame>.ppm` (`P6`, the width and the height on separate lines); the validation layer prints to stderr. This is how `DrawGeometry()` was looked at. Known finding of the validation layer: `VUID-vkQueueSubmit-pSignalSemaphores-00067` (the render-finished semaphores are per frame in flight, not per swapchain image); it is not from the geometry path.

## Build

- **Loader:** Windows links `vulkan-1.lib` from `VULKAN_SDK` when set and present, else from `external/Vulkan-Loader/Windows/Lib`. macOS requires `external/Vulkan-Loader/macOS/lib/libvulkan.1.dylib` and fails configure without it. Linux uses the system loader. `*.dll` files are git-ignored; populate them as described in `external/Vulkan-Loader/README.md`. `VULKAN_SDK` is a per-machine cache value (default in `cmake/BagiEngine.cmake`), override it in `CMakeUserPresets.json`.
- **Shaders:** `shaders/<name>.<stage>.glsl` → `glslc` → `.spv` → `cmake/SpvToHeader.cmake` → `<name>_<stage>_spv.h` with a `constexpr uint32_t` array in the build directory (private include path). A new shader is one `compile_shader(<stage> <name> <kVarName>)` line in `CMakeLists.txt` plus its header in the `VulkanShaders` target's `DEPENDS`. `glslc` comes from `VULKAN_SDK`, else `PATH`; do not pre-set `_GLSLC` to `""` (the search is then skipped).
- The target needs `IMGUI_VULKAN_AVAILABLE=1` consistent with `BECoreModule` because it reuses its PCH.

## Rules

- Keep Vulkan types inside this module. `BECore` headers only see `IRenderer` / `ITexture` / `IRenderTarget`; backend handles do not appear in scene or component APIs.
- A new `IRenderer` capability is added to the interface first and implemented in both backends (rendering rule).
- Add a new Vulkan class to `CoreVulkan/` and let `GLOB_RECURSE` pick it up; re-run `cmake --preset ...`.
