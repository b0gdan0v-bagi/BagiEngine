#pragma once

#include <Core/MainWindow/IMainWindow.h>

namespace Core {
    struct SetRenderDrawColorEvent;
    class IRendererHolder;

    class SDLMainWindow : public IMainWindow {
    public:
        SDLMainWindow();
        ~SDLMainWindow() override;

        bool Initialize(std::string_view configPath) override;
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

        void SetRenderDrawColor(const SetRenderDrawColorEvent& event) const;

    private:
        void RenderClear() const;
        void RenderPresent() const;

        SDL_Window* _window = nullptr;
        SDL_Renderer* _renderer = nullptr;
        int _width = 0;
        int _height = 0;
    };
}  // namespace Core
