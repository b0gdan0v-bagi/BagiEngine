---
paths:
  - "src/Modules/BECore/Renderer/**/*"
  - "src/Modules/BECore/Scene/Components/*RendererComponent*"
  - "src/Modules/SDL/src/CoreSDL/*Renderer*"
  - "src/Modules/SDL/src/CoreSDL/*Texture*"
  - "src/Modules/Vulkan/**/*"
---

# Rendering Module Guidelines

## Layer Responsibilities

- `BECore/Renderer` owns the engine-level abstractions: `IRenderer`, `ITexture`, `IRenderTarget`, `RendererManager`.
- Backend modules provide the implementations: `SDL` (`SDLRendererBackend`, `SDLTexture`) and `Vulkan` (`VulkanRendererBackend`, `VulkanTexture`, `VulkanRenderTarget`, swapchain and pipelines).
- Scene components (`QuadRendererComponent`, `SpriteRendererComponent`) consume the abstractions, not backend types.
- The backend is chosen at startup by the `renderer` attribute of `config/ApplicationConfig.xml` (`SDLRendererBackend` or `VulkanRendererBackend`); `IRenderer` is a `FACTORY_BASE`, so a new backend is just another registered `IRenderer`.
- `IRenderTarget` is an offscreen target shown as an ImGui image (the viewport). Obtain it from `IRenderer::CreateRenderTarget()`; do not assume a backend behind `GetImGuiTextureId()`.

## Texture Pipeline

- Texture loading goes through dedicated `IResourceLoader` classes (`SDLTextureLoader`, `VulkanTextureLoader`), so it follows `src/Modules/BECore/Resource/CLAUDE.md`.
- Keep texture lifetime and ownership behind `ITexture`. Do not expose backend handles in scene/component public APIs (`GetNativeHandle()` is for backend code).
- Named sprites come from `TextureLibrary` (`config/TextureLibrary.xml`): name → texture path + source rect.

## Serialization and Components

- Texture-related component fields must be serialization-friendly and stable: sprite id / path / source rect, never a backend runtime handle (see `SpriteRendererComponent`).
- Serialize the data needed to rebuild the texture state, not transient backend objects.

## Cross-Backend Compatibility

- Define new renderer-facing behavior at the `IRenderer` / `ITexture` level first, then implement it in both backends where supported.
- If a backend cannot support a feature, document the fallback explicitly (skip draw, placeholder, or warning).
- ImGui reaches the backend through `RenderEvents::ImGui*Event`, not direct calls (see `src/Modules/SDL/CLAUDE.md`).

## Vulkan Shaders

GLSL sources live in `Vulkan/shaders/*.glsl`. At build time `glslc` (from `VULKAN_SDK`, else on `PATH`) compiles them to SPIR-V and `Vulkan/cmake/SpvToHeader.cmake` embeds them as `constexpr uint32_t[]` headers in the build directory. A new shader needs an entry in `Vulkan/CMakeLists.txt`.

## Error Handling

- Validate resource loading and creation paths; log failures with enough context to trace the asset and the component.
- Avoid silent render failures: log a warning/error and apply a predictable fallback.
