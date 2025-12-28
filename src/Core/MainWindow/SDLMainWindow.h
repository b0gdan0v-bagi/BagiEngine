#pragma once

#include <Core/MainWindow/IMainWindow.h>

namespace Core {
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

        const IntrusivePtr<IRendererHolder>& GetRenderer() const override {
            return _renderer;
        }

        void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) override;

    private:
        void RenderClear() const;
        void RenderPresent() const;

        SDL_Window* _window = nullptr;
        IntrusivePtr<IRendererHolder> _renderer;
        int _width = 0;
        int _height = 0;
    };
}  // namespace Core
