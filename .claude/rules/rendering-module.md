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

## Drawing primitives

- **Tint:** `DrawTexture(texture, src, x, y, w, h, tint)` multiplies the texel by `tint` (white leaves it, the alpha of the tint fades it). It is the virtual one; the six-argument overload is a non-virtual helper that forwards with white, so a backend adds `using IRenderer::DrawTexture;` next to its override (the override hides the helper otherwise). The method is not `BE_FUNCTION`: the generated method table takes `&IRenderer::DrawTexture`, which an overloaded name makes ambiguous. A backend must not leave a tint on a shared texture: SDL sets the color and alpha modulation on every draw.
- **Alpha:** `DrawFilledRect()` and `DrawTexture()` blend straight alpha over the target in both backends (the SDL backend sets `SDL_BLENDMODE_BLEND` on the rect path and on the textures it makes).
- **Clip:** `PushClipRect(rect)` / `PopClipRect()` keep a stack on the interface. The rect is in **output pixels, after the view transform** (a scene transform does not move it; code that has a rect in scene units maps it with `GetViewTransform().ToOutput(...)` first). Pushed rects nest: each entry is already intersected with the one below. A push that leaves nothing gives an empty clip (`IsClipEmpty()`), and both backends then skip every draw. A backend reads `GetClipRect()`: SDL applies it in `OnClipRectChanged()`, Vulkan resolves it into the scissor of each draw (`ResolveScissor()`, cut to the extent). An unbalanced push leaks into the next frame, so pair them at the call site.
- **`DrawGeometry(texture, vertices, indices, translateX, translateY)`:** indexed triangles (`GeometryVertex`: position, straight-alpha `Color`, UV 0 to 1), the primitive of a library that brings its own layout (RmlUi, `src/Modules/RmlUiHost/CLAUDE.md`). Each position is moved by the translation, then by the view transform; the draw obeys the clip; the texture is optional (without one the vertex colors are the fill). Not `BE_FUNCTION` (spans). It is virtual and not pure: the default logs once and draws nothing, for a backend without the primitive (both real backends have it). `SDLRendererBackend` uses `SDL_RenderGeometryRaw`; `VulkanRendererBackend` has a pipeline and a vertex buffer per frame in flight of its own (`src/Modules/Vulkan/CLAUDE.md`, "Geometry"). In both, an index past the vertices is logged and nothing is drawn. `RenderProbeComponent` draws two geometry quads for a look at both. `RecordingRenderer` records it as `DrawKind::Geometry` with the vertices in output pixels.
- **`CreateTexture(rgba, width, height)`:** a texture from pixels (RGBA8, red first, straight alpha, rows from the top, copied). It returns an empty pointer for a zero size or when the backend fails. Main thread only, like every GPU call; never inside `LoadAsync`. Vulkan textures are destroyed only when the GPU is idle (`VulkanTexture`).
- **`GetId()` and `WaitIdle()`:** `IRenderer::GetId()` is unique for the life of the process (a renderer allocated at the address of a dead one has another id); whoever keeps GPU objects of a renderer, such as the glyph atlases, remembers it and drops them when it changes. `WaitIdle()` blocks until the GPU has finished what was submitted (Vulkan: `vkDeviceWaitIdle`; the SDL default does nothing): call it before freeing textures that a frame in flight may use, and free them **before** the renderer is destroyed (`Application::Cleanup()` clears the glyph atlases before it emits `ApplicationCleanUpEvent`; EnTT calls an event's handlers in the reverse order of their subscription, so a handler subscribed earlier than the renderer's would run after its `Destroy()`).
- A test that needs a renderer uses `RecordingRenderer` (`tests/Common/RecordingRenderer.h`): it records the calls in output pixels with the clip in effect, reports a settable output size and keeps the pixels of the textures it makes. `RenderProbeComponent` is the debug component that draws every primitive for a look at both backends.

## Texture Pipeline

- Texture loading goes through dedicated `IResourceLoader` classes (`SDLTextureLoader`, `VulkanTextureLoader`), so it follows `src/Modules/BECore/Resource/CLAUDE.md`. Textures from pixels in memory come from `IRenderer::CreateTexture()` instead and are not cached by path.
- Keep texture lifetime and ownership behind `ITexture`. Do not expose backend handles in scene/component public APIs (`GetNativeHandle()` is for backend code).
- Named sprites come from `TextureLibrary` (`config/TextureLibrary.xml`): name → texture path + source rect.

## Serialization and Components

- Texture-related component fields must be serialization-friendly and stable: sprite id / path / source rect, never a backend runtime handle (see `SpriteRendererComponent`).
- Serialize the data needed to rebuild the texture state, not transient backend objects.

## Cross-Backend Compatibility

- Define new renderer-facing behavior at the `IRenderer` / `ITexture` level first, then implement it in both backends where supported.
- If a backend cannot support a feature, document the fallback explicitly (skip draw, placeholder, or warning). `IRenderer::RequestCapture()` is an example: it is not pure, the default answers with an error, `SDLRendererBackend` reads its pixels back at the present and `VulkanRendererBackend` keeps the default (`src/Modules/Vulkan/CLAUDE.md`, "No capture").
- ImGui reaches the backend through `RenderEvents::ImGui*Event`, not direct calls (see `src/Modules/SDL/CLAUDE.md`).

## Vulkan specifics

Frame lifecycle, loader linking and the shader pipeline (GLSL → SPIR-V → embedded header) are in `src/Modules/Vulkan/CLAUDE.md`.

## Error Handling

- Validate resource loading and creation paths; log failures with enough context to trace the asset and the component.
- Avoid silent render failures: log a warning/error and apply a predictable fallback.
