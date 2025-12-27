#pragma once

#include <Core/MainWindow/IRendererHolder.h>

namespace Core {
    class SDLRendererHolder : public IRendererHolder {
    public:
        SDLRendererHolder();
        ~SDLRendererHolder() override;

        bool Create(const IMainWindow& window) override;
        void Destroy() override;

        SDL_Renderer* GetRenderer() const {
            return _renderer;
        }

        void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) override;
        void RenderClear() override;
        void RenderPresent() override;

    private:
        SDL_Renderer* _renderer = nullptr;
    };
}  // namespace Core
