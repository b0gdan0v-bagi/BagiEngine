#pragma once

#include <BECore/Renderer/IRenderer.h>

struct SDL_Renderer;

namespace BECore {

    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }

    class SDLRendererBackend : public IRenderer {
        BE_CLASS(SDLRendererBackend)
    public:
        SDLRendererBackend() = default;
        ~SDLRendererBackend() override = default;

        bool Initialize(IMainWindow& window) override;
        void Destroy() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Clear(const Color& color) override;
        void Present() override;
        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override;

        SDL_Renderer* GetSDLRenderer() const {
            return _renderer;
        }

    private:
        void OnRenderClear();
        void OnRenderPresent();
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        SDL_Renderer* _renderer = nullptr;
    };

}  // namespace BECore
