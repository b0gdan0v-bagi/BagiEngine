# Vulkan: Present() with an offscreen pass still open stops every later frame

- Version: 0.25.7
- Entry: vulkan-present-open-offscreen
- Date: 2026-11-25
- Bump: patch

## Changed
- Vulkan `Present()` closes an offscreen pass that is still open (as `UnsetRenderTarget()` does) and logs it once, instead of leaving the frame acquired and freezing the window. Adapt: nothing; a game that leaves a render target set at the end of a frame should still unset it.
