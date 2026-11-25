# Vulkan: the validation layer reports the render-finished semaphores every frame

- Version: 0.25.6
- Entry: vulkan-srgb-semaphores
- Date: 2026-11-25
- Bump: patch

## Changed
- The Vulkan backend keeps one render-finished semaphore per swapchain image instead of per frame slot, so the validation layer no longer reports `VUID-vkQueueSubmit-pSignalSemaphores-00067` every frame; they are made again after each swapchain rebuild. `tools/stress/qa/vulkan_look.py` fails on any validation line. Adapt: nothing.
