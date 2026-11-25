# Frame: the application opens every frame with the clear colour; ClearScreenWidget is gone

- Version: 0.24.0
- Entry: frame-begin-in-application
- Date: 2026-11-25
- Bump: minor

## Added
- `ApplicationConfig::_clearColor` (child element `<clearColor r g b a/>` of `ApplicationConfig.xml`, default `{20, 20, 100, 255}`): the colour every frame starts with. Adapt: a game that set a colour on `ClearScreenWidget` moves it here.

## Breaking
- `IRenderer::BeginFrame()` became `BeginFrame(const Color& clearColor)` and is called by `Application::RunFrame` before any widget; Vulkan opens the swapchain pass at the first draw into the back buffer. Adapt: a custom `IRenderer` (a test double) takes the colour; nothing calls `BeginFrame` but the application.
- `ClearScreenWidget`, `RenderEvents::RenderClearEvent` and `RenderEvents::SetRenderDrawColorEvent` are removed; a widgets config that still names `ClearScreenWidget` logs one unknown-type error and the frame is still drawn. Adapt: drop the `ClearScreenWidget` entry from the game's widgets configs and set `clearColor` in `ApplicationConfig.xml`; a subscriber of the two events uses `IRenderer::Clear` before `SetRenderTarget` for an offscreen target.
