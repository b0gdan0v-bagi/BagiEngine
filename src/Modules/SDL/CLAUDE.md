# SDL Module Guidelines

## Overview

SDL3 integration: window, input events and the SDL renderer backend. `Vulkan` is the alternative backend; both implement the interfaces in `BECore/Renderer` (see `.claude/rules/rendering-module.md` for texture pipeline, serialization and fallback rules).

## Key Components

| Component | Purpose |
|-----------|---------|
| `SDL3ApplicationFabric` | `IApplicationFabric`: creates the window from `SDLWindowConfig.xml`, the renderer named by `renderer` in `config/ApplicationConfig.xml` (default `SDLRendererBackend`) and the events provider |
| `SDLMainWindow` | `IMainWindow` implementation; window only, the renderer is separate |
| `SDLRendererBackend` | `IRenderer` implementation on the SDL renderer; owns the ImGui SDL backends |
| `SDLEventsProvider` / `SDLEvents.h` | SDL event polling and conversion to engine events |
| `SDLTexture` / `SDLTextureLoader` | `ITexture` implementation and its `IResourceLoader` |
| `SDLWindowConfig` | Window settings (`config/SDLWindowConfig.xml`: title, size, `windowFlags`) |
| `SDLUtils` | Shared SDL helpers |

`IMainWindow`, `MainWindowManager` and `MainWindowAccessor` (access to the window and renderer) live in `BECore/MainWindow/`, not here.

## SDL3 vs SDL2 Differences

- Event types use the `SDL_EVENT_` prefix
- Rectangles use `SDL_FRect` (float) by default
- `SDL_Init()` returns `bool`, not `int`

## Best Practices

- **Forward declare SDL types** in headers (`struct SDL_Window;`).
- **Use accessors**: do not store raw SDL pointers long-term.
- **Check return values**: SDL3 functions may fail.
- **Use the event system**: do not poll SDL events outside `SDLEventsProvider`.
- Engine-facing code depends on `IRenderer` / `ITexture`, never on raw SDL handles.

## ImGui + SDL3

The backends are driven by events, not called from widgets. Per frame:

1. `ImGuiWidget::OnNewFrame` emits `RenderEvents::ImGuiNewFrameEvent`.
2. `SDLRendererBackend::OnImGuiNewFrame` calls `ImGui_ImplSDL3_NewFrame()` and then `ImGui_ImplSDLRenderer3_NewFrame()`.
3. `ImGuiWidget` calls `ImGui::NewFrame()`, and the widgets build their windows.
4. `ImGuiWidget` calls `ImGui::Render()` and emits `RenderEvents::ImGuiRenderEvent`; `SDLRendererBackend::OnImGuiRender` calls `ImGui_ImplSDLRenderer3_RenderDrawData(...)`.

Init and shutdown use `ImGuiInitEvent` / `ImGuiShutdownEvent` the same way. `VulkanRendererBackend` subscribes to the same four events, so widget code stays backend-agnostic.

## Error Handling

Validate texture load/create results and report failures through logging/assertions. Prefer an explicit fallback (placeholder texture, skip draw, disabled component) over silent failure. Resolve asset paths through the `FileSystem`, not ad-hoc relative paths.
