---
id: the-render-output-size-is-fixed-at-startup-the-vulkan
---
# The render output size is fixed at startup: the Vulkan swapchain is never recreated, and there is no HiDPI

**Where:** `src/Modules/Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp` (`AcquireFrame()`: "swapchain out-of-date (TODO: recreate on resize)"; `Present()` ignores the result of `VulkanSwapchain::Present`), `VulkanSwapchain.cpp` (`ChooseExtent`), `SDLEventsProvider.cpp` (the `SDL_EVENT_WINDOW_RESIZED` case sits in a commented-out block, so no resize event is emitted), `config/SDLWindowConfig.xml` (no `SDL_WINDOW_HIGH_PIXEL_DENSITY`).

**What is wrong** (found by reading the code, not reproduced by resizing a running window):

1. The swapchain is created once, with the extent the surface reports at that moment. When the window changes size, `AcquireNextImage` returns "out of date", `AcquireFrame()` returns without recreating anything and the frame is skipped. The compositor may stretch the old image instead, and then the picture is distorted. `Present()` does not look at the result either.
2. Since the scene view fit (`Scene::Draw()`, `IRenderer::GetOutputWidth()`), the picture follows the output size, so a recreated swapchain is all that is missing: nothing else has to learn about the new size.
3. The window has no `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so on a Retina display the output is in window points and the OS scales it up by 2 (measured from a screenshot of `BagiChess`: exactly 2 px per scene unit; not checked in MoltenVK or SDL code). The picture is soft, and screenshots of the control API will be point-sized.

**A fix has to cover:**

- recreate the swapchain, its image views and framebuffers (the render pass only when the format changes) on out-of-date / suboptimal and after a resize, with `vkDeviceWaitIdle` first; skip drawing while the window is minimized (extent 0);
- a decision on HiDPI: the output in pixels with a scale factor for the scene view, or points as today (the editor viewport and ImGui scale differently);
- check `Present` and `AcquireNextImage` results in one place;
- a manual check list (maximize, resize by hand, minimize and restore, move between displays), because no test creates a Vulkan device.
