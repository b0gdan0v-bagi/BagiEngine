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

## Output size and view transform

- `IRenderer::GetOutputWidth()` / `GetOutputHeight()` are the pixel size of the target being drawn into (the back buffer, or the offscreen target between `SetRenderTarget()` and `UnsetRenderTarget()`); they are 0 before the first pass opens.
- `IRenderer::SetViewTransform()` sets a `ViewTransform` (scale and offset, identity by default) that `DrawFilledRect()` and `DrawTexture()` apply to their destination rect. It is state of the interface, so both backends apply it the same way (`GetViewTransform().ToOutput(x, y, w, h)`) and a new backend must do the same. `Scene::Draw()` sets it around its draw pass and resets it, so a scene with a view size fits any output (`src/Modules/BECore/Scene/CLAUDE.md`, "View size"). Only the destination is mapped: source rects and UVs are untouched.

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

## Vulkan specifics

Frame lifecycle, loader linking and the shader pipeline (GLSL → SPIR-V → embedded header) are in `src/Modules/Vulkan/CLAUDE.md`.

## Error Handling

- Validate resource loading and creation paths; log failures with enough context to trace the asset and the component.
- Avoid silent render failures: log a warning/error and apply a predictable fallback.
