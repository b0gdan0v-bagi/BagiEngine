#pragma once

#include <BECore/MainWindow/IMainWindow.h>
#include <SDL3/SDL.h>

namespace BECore {
    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }
    class IRendererHolder;

    class SDLMainWindow : public IMainWindow, public SubscriptionHolder {
    public:
        SDLMainWindow();
        ~SDLMainWindow() override;

        bool Initialize(eastl::string_view configPath) override;
        void Destroy() override;
        bool IsValid() const override;
        int GetWidth() const override;
        int GetHeight() const override;

        SDL_Window* GetSDLWindow() const {
            return _window;
        }

        SDL_Renderer* GetSDLRenderer() const {
            return _renderer;
        }

        void SetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event) const;

    private:
        void RenderClear() const;
        void RenderPresent() const;

        SDL_Window* _window = nullptr;
        SDL_Renderer* _renderer = nullptr;
        int _width = 0;
        int _height = 0;
    };
}  // namespace BECore
