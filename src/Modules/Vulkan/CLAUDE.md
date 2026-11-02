# Vulkan Module Guidelines

## Overview

`VulkanModule` is the second renderer backend: `VulkanRendererBackend` implements `IRenderer` (see `.claude/rules/rendering-module.md` for the rules shared with SDL). It is selected by `renderer="VulkanRendererBackend"` in `config/ApplicationConfig.xml`; the window itself still comes from `SDLMainWindow` (`SDL_WINDOW_VULKAN` in `config/SDLWindowConfig.xml`). Links `BECoreModule`, `EventsModule`, `SDLModule` and `Vulkan::Headers`; every game executable links it with `WHOLE_ARCHIVE` (`be_add_game_executable`).

## Components (`src/CoreVulkan/`)

| Class | Role |
|-------|------|
| `VulkanRendererBackend` | `IRenderer`: frame lifecycle, command buffers, per-frame sync (`kMaxFramesInFlight = 2`), ImGui backend. `GetOutputWidth/Height()` is the extent of the open pass (swapchain or offscreen target); `DrawFilledRect` / `DrawTexture` apply `IRenderer::GetViewTransform()` to the rect on the CPU, so the shaders always get output pixels; `ResolveScissor()` turns the clip stack into the scissor of each draw (whole extent when unclipped, draw skipped when the clip is empty); `CreateTexture()` goes through `VulkanTextureUpload`; `WaitIdle()` is `vkDeviceWaitIdle` (a no-op once the device is destroyed) |
| `VulkanDevice` | Instance, GPU pick, logical device. Two-step init: `InitializeInstance()`, then `InitializeDevice(surface)` (the surface needs the instance). Memory-type lookup and single-time command helpers |
| `VulkanSwapchain` | Swapchain and its render pass. `AcquireNextImage` / `Present` return `false` when the swapchain is out of date |
| `VulkanRectPipeline`, `VulkanTexturePipeline` | Graphics pipelines for `DrawFilledRect` and `DrawTexture`. Both take the scissor of the draw as a parameter. The texture pipeline's push constants are 56 bytes: `dstRect`, `srcUV`, `tint` (vec4, multiplied with the texel in `texture.frag.glsl`), `screenSize`; the vertex and fragment shaders declare the same block |
| `VulkanTextureUpload` | Pixels in memory to a `VulkanTexture` (staging buffer, device-local image, view, sampler, descriptor set, one single-time command buffer). Shared by `VulkanTextureLoader` (pixels decoded from a file) and `CreateTexture()`; main thread only |
| `VulkanTexture` | `ITexture`: image + memory + view + sampler + descriptor set (set 0). Destroy it only when the GPU is idle |
| `VulkanTextureLoader` | `IResourceLoader` for `.bmp .png .jpg .jpeg`; `CanLoad` is true **only while the active renderer is the Vulkan backend** (`SDLTextureLoader` does the mirror check). It decodes and converts the file, `VulkanTextureUpload` uploads. Pixels are uploaded as `SDL_PIXELFORMAT_RGBA32` (bytes R,G,B,A) to match `VK_FORMAT_R8G8B8A8_SRGB`; the packed `RGBA8888` would swap the channels on little-endian |
| `VulkanRenderTarget` | `IRenderTarget`: offscreen target; `GetImGuiTextureId()` is a `VkDescriptorSet` from `ImGui_ImplVulkan_AddTexture` |

## Frame lifecycle

Driven by events, not by direct calls:

1. `RenderEvents::NewFrameEvent` → `AcquireFrame()`: acquire the swapchain image and open the command buffer. This happens **before** the swapchain render pass so `ViewportWidget` can insert an offscreen pass first.
2. `SetRenderTarget` / `UnsetRenderTarget` open and close an offscreen pass (the barrier on the target image is recorded in `UnsetRenderTarget`).
3. `RenderClearEvent` → begin the swapchain pass; widgets record their draw commands into it. In Game run mode there is no offscreen pass: `SceneRenderWidget` draws the scene into this pass in `DrawScreen()`.
4. `RenderPresentEvent` → end the pass, submit, present.

ImGui goes through `ImGuiInit/NewFrame/Render/ShutdownEvent` like the SDL backend (`imgui_impl_vulkan`, which is why the backend exposes `GetVk*` getters). `ApplicationCleanUpEvent` calls `Destroy()`.

## No capture

`VulkanRendererBackend` does not implement `IRenderer::RequestCapture()`: the default answers with an error, and the `screenshot` method of the control API therefore answers `capture_failed` on this backend. A screenshot needs `--headless` (the SDL software renderer) until a Vulkan readback is written. The decision on the backends (`TODO.md`, "Renderer backends") comes first.

## No geometry

`VulkanRendererBackend` does not override `IRenderer::DrawGeometry()` (indexed triangles with a color and a UV per vertex): the default logs once and draws nothing, so what needs it (the RmlUi host, `src/Modules/RmlUiHost/CLAUDE.md`) shows on the SDL backend only. It needs a pipeline of its own (a vertex buffer in place of the push-constant rect) and a dynamic vertex buffer per frame in flight; the texture pipeline's tint and scissor already fit it.

## Build

- **Loader:** Windows links `vulkan-1.lib` from `VULKAN_SDK` when set and present, else from `external/Vulkan-Loader/Windows/Lib`. macOS requires `external/Vulkan-Loader/macOS/lib/libvulkan.1.dylib` and fails configure without it. Linux uses the system loader. `*.dll` files are git-ignored; populate them as described in `external/Vulkan-Loader/README.md`. `VULKAN_SDK` is a per-machine cache value (default in `cmake/BagiEngine.cmake`), override it in `CMakeUserPresets.json`.
- **Shaders:** `shaders/<name>.<stage>.glsl` → `glslc` → `.spv` → `cmake/SpvToHeader.cmake` → `<name>_<stage>_spv.h` with a `constexpr uint32_t` array in the build directory (private include path). A new shader is one `compile_shader(<stage> <name> <kVarName>)` line in `CMakeLists.txt` plus its header in the `VulkanShaders` target's `DEPENDS`. `glslc` comes from `VULKAN_SDK`, else `PATH`; do not pre-set `_GLSLC` to `""` (the search is then skipped).
- The target needs `IMGUI_VULKAN_AVAILABLE=1` consistent with `BECoreModule` because it reuses its PCH.

## Rules

- Keep Vulkan types inside this module. `BECore` headers only see `IRenderer` / `ITexture` / `IRenderTarget`; backend handles do not appear in scene or component APIs.
- A new `IRenderer` capability is added to the interface first and implemented in both backends (rendering rule).
- Add a new Vulkan class to `CoreVulkan/` and let `GLOB_RECURSE` pick it up; re-run `cmake --preset ...`.
