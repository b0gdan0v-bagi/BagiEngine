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
        float GetPixelDensity() const override;
        float GetDisplayScale() const override;
        Rect GetSafeArea() const override;

        /**
         * @brief Makes the window a headless one: call it before Initialize().
         *
         * Initialize() then asks SDL for its dummy video driver (no real window appears, nothing is shown) and takes
         * SDL_WINDOW_VULKAN out of the configured flags, which that driver cannot honour.
         */
        void SetHeadless(bool headless) {
            _headless = headless;
        }

        [[nodiscard]] bool IsHeadless() const {
            return _headless;
        }

        SDL_Window* GetSDLWindow() const {
            return _window;
        }

    private:
        void RenderPresent() const;

        SDL_Window* _window = nullptr;
        int _width = 0;
        int _height = 0;
        bool _headless = false;
    };

}  // namespace BECore
