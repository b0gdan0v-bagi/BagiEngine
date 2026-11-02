---
stage: 4
id: renderer-backends-one-decision-then-batching
---
# Renderer backends: one decision, then batching

**Where:** `src/Modules/BECore/Renderer/IRenderer.h`, `src/Modules/SDL/`, `src/Modules/Vulkan/`, `external/Vulkan-Loader/` (vendored loader for macOS, which runs Vulkan on top of Metal).

**What is wrong** (found by reading the code and docs):

1. Every new `IRenderer` capability has to be written twice, in `SDLRendererBackend` and `VulkanRendererBackend` (`rendering-module.md`). For a 2D engine this doubles the cost of every item in [the 2D rendering entry](rendering-2d.md).
2. A draw is one `IRenderer` call per object; there is no batching, atlas support or sort pass in the interface.
3. **Vulkan colors differ from SDL's** (found 2026-11-02, looking at the same scene on both backends with the LunarG screenshot layer, `Vulkan/CLAUDE.md`, "Looking at the pixels"): the swapchain is `B8G8R8A8_SRGB`, textures are `R8G8B8A8_SRGB` (they round-trip exactly), but the rect pipeline, the tint, the vertex colors of `DrawGeometry()` and the clear color are passed as raw 0 to 1 numbers, which the hardware reads as linear and encodes again. `Color(40, 90, 60)` is drawn as about (110, 160, 135), and a clear color of (30, 30, 30) as about (96, 96, 96). Every screen of the games looks washed out on Vulkan next to SDL. Fixes: convert sRGB to linear in the rect and geometry vertex shaders and for the clear color (colors stay sRGB in data, as everywhere else), or pick a `UNORM` swapchain and treat textures as `UNORM` too. It changes how everything looks on Vulkan, so it waits for the decision below.
4. **Validation layer** (same run): `VUID-vkQueueSubmit-pSignalSemaphores-00067` every frame, because the render-finished semaphores are one per frame in flight and not one per swapchain image (https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html). Also: a texture made by `CreateTexture()` and held by a scene component outlives the Vulkan device (`Application::Cleanup()` clears only the glyph atlases before `ApplicationCleanUpEvent`; components are destroyed with `CoreManager`, after the device), so `vkDestroyDevice` reports it as leaked and its destructor calls into a destroyed device. `RenderProbeComponent` now frees its texture on that event; a component that keeps a GPU texture of its own has to do the same.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- decide which of: (a) the SDL renderer is the main backend and Vulkan stays frozen at its current feature level, (b) keep both and implement new features in one backend first, (c) replace both with the SDL3 GPU API, which gives native Metal on macOS instead of the loader. Recommendation: a 1-2 day spike on (c), and (a) until it is decided;
- after the decision, sprite batching and texture atlases (`TextureLibrary` already maps a sprite name to a texture and a source rect);
- record the decision in `rendering-module.md` and `Vulkan/CLAUDE.md`.
