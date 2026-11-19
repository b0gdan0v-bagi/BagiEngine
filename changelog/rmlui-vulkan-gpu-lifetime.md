# Rendering: RmlUi textures are destroyed while the GPU may still use them; Vulkan results are ignored; one descriptor pool of 512 sets

- Version: 0.6.3
- Entry: rmlui-vulkan-gpu-lifetime
- Date: 2026-11-19
- Bump: patch

## Changed
- RmlUi frees its textures only after `IRenderer::WaitIdle()`: removing the last `RmlUiDocumentComponent` while the game runs no longer frees font pages a frame in flight still samples.
- The RmlUi mouse wheel scrolls horizontally the way SDL reports it, and an `SDL_MOUSEWHEEL_FLIPPED` event scrolls the other way.
- `IRenderer::Clear(color)` is documented as the colour the next pass starts with. On Vulkan the colour is taken by one pass and then goes back to opaque black, so the editor viewport's clear no longer tints the back buffer. Adapt: a game that draws to the swapchain after an offscreen pass on Vulkan calls `Clear` again before `BeginFrame` if it wants a colour other than black.
- Vulkan: the results of `vkQueueSubmit`, `vkWaitForFences`, `vkBeginCommandBuffer`, `vkBindBufferMemory`, `vkBindImageMemory` and `vkMapMemory` are checked; a failed submit drops the frame instead of hanging the next one, and a lost device stops drawing with a log line.
- Vulkan: textures are no longer limited to 512 (descriptor pools are added as needed), all textures share one sampler, and the three 2D pipelines blend alpha with "over", so translucent drawing in an offscreen target leaves no hole in its alpha.
