#include "SDLRendererBackend.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/Renderer/IRenderTarget.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLTexture.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/SDLRendererBackend.gen.hpp>
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kSoftwareRenderer = "software";
        constexpr uint32_t kBytesPerPixel = 4;
        constexpr float kChannelMax = 255.0f;
        constexpr size_t kIndicesPerTriangle = 3;
        constexpr int kFloatsPerColor = 4;

    }  // namespace

    bool SDLRendererBackend::Initialize(IMainWindow& window) {
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(&window);
        if (!sdlWindow) {
            return false;
        }

        // A headless window has no GPU surface behind it: only the software renderer works there
        _renderer = SDL_CreateRenderer(sdlWindow->GetSDLWindow(), sdlWindow->IsHeadless() ? kSoftwareRenderer : nullptr);
        if (!_renderer) {
            LOG_ERROR("SDLRendererBackend: cannot create the renderer: {}"_format(SDL_GetError()));
            return false;
        }

        Subscribe<RenderEvents::RenderClearEvent, &SDLRendererBackend::OnRenderClear>(this);
        Subscribe<RenderEvents::RenderPresentEvent, &SDLRendererBackend::OnRenderPresent>(this);
        Subscribe<RenderEvents::SetRenderDrawColorEvent, &SDLRendererBackend::OnSetRenderDrawColor>(this);
        Subscribe<RenderEvents::ImGuiInitEvent, &SDLRendererBackend::OnImGuiInit>(this);
        Subscribe<RenderEvents::ImGuiShutdownEvent, &SDLRendererBackend::OnImGuiShutdown>(this);
        Subscribe<RenderEvents::ImGuiNewFrameEvent, &SDLRendererBackend::OnImGuiNewFrame>(this);
        Subscribe<RenderEvents::ImGuiRenderEvent, &SDLRendererBackend::OnImGuiRender>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLRendererBackend::Destroy>(this);

        return true;
    }

    void SDLRendererBackend::Destroy() {
        _captureCallback = nullptr;  // nothing will be presented any more
        if (_renderer) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
    }

    void SDLRendererBackend::BeginFrame() {}

    void SDLRendererBackend::EndFrame() {}

    void SDLRendererBackend::Clear(const Color& color) {
        if (_renderer) {
            SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
            SDL_RenderClear(_renderer);
        }
    }

    void SDLRendererBackend::Present() {
        PresentFrame();
    }

    void SDLRendererBackend::DrawFilledRect(float x, float y, float w, float h, const Color& color) {
        if (_renderer && !IsClipEmpty()) {
            SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
            const Rect out = GetViewTransform().ToOutput(x, y, w, h);
            SDL_FRect rect{out.x, out.y, out.w, out.h};
            SDL_RenderFillRect(_renderer, &rect);
        }
    }

    void SDLRendererBackend::DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) {
        if (!_renderer || IsClipEmpty()) {
            return;
        }
        auto* sdlTexture = dynamic_cast<SDLTexture*>(&texture);
        if (!sdlTexture || !sdlTexture->GetSDLTexture()) {
            return;
        }

        // The texture is shared, so its modulation is set on every draw: an untinted draw after a tinted one must not inherit it
        SDL_Texture* handle = sdlTexture->GetSDLTexture();
        SDL_SetTextureColorMod(handle, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(handle, tint.a);

        const Rect out = GetViewTransform().ToOutput(dstX, dstY, dstW, dstH);
        SDL_FRect dst{out.x, out.y, out.w, out.h};

        if (srcRect && !srcRect->IsEmpty()) {
            SDL_FRect src{srcRect->x, srcRect->y, srcRect->w, srcRect->h};
            SDL_RenderTexture(_renderer, handle, &src, &dst);
        } else {
            SDL_RenderTexture(_renderer, handle, nullptr, &dst);
        }
    }

    IntrusivePtr<ITexture> SDLRendererBackend::CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) {
        if (!_renderer || !rgba || width == 0 || height == 0) {
            return {};
        }
        // RGBA32 is the byte order R,G,B,A on any endianness, which is what the caller hands over
        SDL_Texture* handle = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, static_cast<int>(width), static_cast<int>(height));
        if (!handle) {
            LOG_ERROR("SDLRendererBackend: cannot create a {}x{} texture: {}"_format(width, height, SDL_GetError()));
            return {};
        }
        if (!SDL_UpdateTexture(handle, nullptr, rgba, static_cast<int>(width * kBytesPerPixel))) {
            LOG_ERROR("SDLRendererBackend: cannot fill a {}x{} texture: {}"_format(width, height, SDL_GetError()));
            SDL_DestroyTexture(handle);
            return {};
        }
        SDL_SetTextureBlendMode(handle, SDL_BLENDMODE_BLEND);

        auto texture = New<SDLTexture>();
        texture->SetLoaded("<memory>"_intern, handle, static_cast<float>(width), static_cast<float>(height));
        return texture;
    }

    void SDLRendererBackend::DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY) {
        if (!_renderer || IsClipEmpty() || vertices.empty() || indices.size() < kIndicesPerTriangle) {
            return;
        }
        SDL_Texture* handle = nullptr;
        if (texture) {
            const auto* sdlTexture = dynamic_cast<SDLTexture*>(texture);
            if (!sdlTexture || !sdlTexture->GetSDLTexture()) {
                return;
            }
            handle = sdlTexture->GetSDLTexture();
        }
        const int vertexCount = static_cast<int>(vertices.size());
        for (const int index : indices) {
            if (index < 0 || index >= vertexCount) {
                LOG_ERROR("SDLRendererBackend: DrawGeometry got the index {} for {} vertices, so nothing is drawn"_format(index, vertexCount));
                return;
            }
        }

        const ViewTransform& view = GetViewTransform();
        _geometryPositions.resize(vertices.size() * 2);
        _geometryColors.resize(vertices.size() * kFloatsPerColor);
        _geometryTexCoords.resize(vertices.size() * 2);
        for (size_t i = 0; i < vertices.size(); ++i) {
            const GeometryVertex& vertex = vertices[i];
            _geometryPositions[i * 2] = (vertex.x + translateX) * view.scale + view.offsetX;
            _geometryPositions[i * 2 + 1] = (vertex.y + translateY) * view.scale + view.offsetY;
            _geometryColors[i * kFloatsPerColor] = static_cast<float>(vertex.color.r) / kChannelMax;
            _geometryColors[i * kFloatsPerColor + 1] = static_cast<float>(vertex.color.g) / kChannelMax;
            _geometryColors[i * kFloatsPerColor + 2] = static_cast<float>(vertex.color.b) / kChannelMax;
            _geometryColors[i * kFloatsPerColor + 3] = static_cast<float>(vertex.color.a) / kChannelMax;
            _geometryTexCoords[i * 2] = vertex.u;
            _geometryTexCoords[i * 2 + 1] = vertex.v;
        }

        // An untextured draw blends by the draw mode; a texture by its own (the colors multiply the texel, SDL ignores the modulation of the texture here)
        SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_BLEND);
        const bool drawn =
            SDL_RenderGeometryRaw(_renderer, handle, _geometryPositions.data(), 2 * sizeof(float), reinterpret_cast<const SDL_FColor*>(_geometryColors.data()), kFloatsPerColor * sizeof(float),
                                  _geometryTexCoords.data(), 2 * sizeof(float), vertexCount, indices.data(), static_cast<int>(indices.size() - indices.size() % kIndicesPerTriangle), sizeof(int));
        if (!drawn) {
            LOG_ERROR("SDLRendererBackend: DrawGeometry failed: {}"_format(SDL_GetError()));
        }
    }

    void SDLRendererBackend::OnClipRectChanged() {
        if (!_renderer) {
            return;
        }
        const std::optional<Rect> clip = GetClipRect();
        if (!clip) {
            SDL_SetRenderClipRect(_renderer, nullptr);
            return;
        }
        // Grow to whole pixels, so a rect on a fraction never cuts a pixel that it touches
        const int left = static_cast<int>(std::floor(clip->x));
        const int top = static_cast<int>(std::floor(clip->y));
        const int right = static_cast<int>(std::ceil(clip->x + clip->w));
        const int bottom = static_cast<int>(std::ceil(clip->y + clip->h));
        const SDL_Rect rect{left, top, right - left, bottom - top};
        SDL_SetRenderClipRect(_renderer, &rect);
    }

    IntrusivePtr<IRenderTarget> SDLRendererBackend::CreateRenderTarget(uint32_t /*width*/, uint32_t /*height*/) {
        // Not yet implemented for the SDL backend.
        return {};
    }

    void SDLRendererBackend::SetRenderTarget(IRenderTarget* /*target*/) {}

    void SDLRendererBackend::UnsetRenderTarget() {}

    float SDLRendererBackend::GetOutputWidth() const {
        int width = 0;
        int height = 0;
        if (_renderer) {
            SDL_GetCurrentRenderOutputSize(_renderer, &width, &height);
        }
        return static_cast<float>(width);
    }

    float SDLRendererBackend::GetOutputHeight() const {
        int width = 0;
        int height = 0;
        if (_renderer) {
            SDL_GetCurrentRenderOutputSize(_renderer, &width, &height);
        }
        return static_cast<float>(height);
    }

    void SDLRendererBackend::OnRenderClear() {
        if (_renderer) {
            SDL_RenderClear(_renderer);
        }
    }

    void SDLRendererBackend::OnRenderPresent() {
        PresentFrame();
    }

    void SDLRendererBackend::PresentFrame() {
        if (_renderer) {
            ServeCapture();  // the pixels have to be read before the present: after it the back buffer is undefined
            SDL_RenderPresent(_renderer);
        }
    }

    void SDLRendererBackend::RequestCapture(CaptureCallback callback) {
        if (_captureCallback) {
            CaptureCallback superseded = std::move(_captureCallback);
            superseded(std::unexpected(eastl::string("the capture was replaced by a newer request")));
        }
        _captureCallback = std::move(callback);
    }

    void SDLRendererBackend::ServeCapture() {
        if (!_captureCallback) {
            return;
        }
        CaptureCallback callback = std::move(_captureCallback);
        _captureCallback = nullptr;

        SDL_Surface* surface = SDL_RenderReadPixels(_renderer, nullptr);
        if (!surface) {
            callback(std::unexpected(Format("SDL_RenderReadPixels failed: {}", SDL_GetError())));
            return;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (!rgba) {
            callback(std::unexpected(Format("converting the captured pixels failed: {}", SDL_GetError())));
            return;
        }

        PixelBuffer image;
        image.width = static_cast<uint32_t>(rgba->w);
        image.height = static_cast<uint32_t>(rgba->h);
        const size_t rowBytes = static_cast<size_t>(rgba->w) * 4;
        image.rgba.resize(rowBytes * static_cast<size_t>(rgba->h));
        const auto* source = static_cast<const uint8_t*>(rgba->pixels);
        for (int row = 0; row < rgba->h; ++row) {  // the surface may pad its rows
            std::copy_n(source + static_cast<size_t>(row) * static_cast<size_t>(rgba->pitch), rowBytes, image.rgba.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(row) * rowBytes));
        }
        SDL_DestroySurface(rgba);
        callback(std::move(image));
    }

    void SDLRendererBackend::OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event) {
        if (_renderer) {
            SDL_SetRenderDrawColor(_renderer, event.color.r, event.color.g, event.color.b, event.color.a);
        }
    }

    void SDLRendererBackend::OnImGuiInit() {
        if (!_renderer) {
            return;
        }
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return;
        }
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(window.Get());
        if (!sdlWindow) {
            return;
        }
        ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow->GetSDLWindow(), _renderer);
        ImGui_ImplSDLRenderer3_Init(_renderer);
    }

    void SDLRendererBackend::OnImGuiShutdown() {
        ImGui_ImplSDLRenderer3_Shutdown();
    }

    void SDLRendererBackend::OnImGuiNewFrame() {
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
    }

    void SDLRendererBackend::OnImGuiRender() {
        if (_renderer) {
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _renderer);
        }
    }

}  // namespace BECore
