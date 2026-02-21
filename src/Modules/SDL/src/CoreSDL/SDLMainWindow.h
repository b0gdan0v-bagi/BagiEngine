#pragma once

#include <BECore/MainWindow/IMainWindow.h>
#include <SDL3/SDL.h>

namespace BECore {

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

    private:
        void RenderPresent() const;

        SDL_Window* _window = nullptr;
        int _width = 0;
        int _height = 0;
    };

}  // namespace BECore
