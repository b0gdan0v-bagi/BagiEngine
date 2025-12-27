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
        void* GetNativeWindow() const override;
        int GetWidth() const override;
        int GetHeight() const override;

        SDL_Window* GetSDLWindow() const {
            return _window;
        }

        const IntrusivePtr<IRendererHolder>& GetRenderer() const override {
            return _renderer;
        }

        void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b,
                                unsigned char a) override;
        void RenderClear() override;
        void RenderPresent() override;

       private:
        SDL_Window* _window = nullptr;
        IntrusivePtr<IRendererHolder> _renderer;
        int _width = 0;
        int _height = 0;
    };
}  // namespace Core
