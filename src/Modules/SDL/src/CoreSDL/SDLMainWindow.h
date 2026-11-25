#pragma once

#include <BECore/Application/RenderBackendChoice.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Math/Rect.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <SDL3/SDL.h>

namespace BECore {

    class SDLMainWindow : public IMainWindow, public SubscriptionHolder {
    public:
        SDLMainWindow();
        ~SDLMainWindow() override;

        bool Initialize(StringView configPath) override;
        void Destroy() override;
        bool IsValid() const override;
        int GetWidth() const override;
        int GetHeight() const override;
        float GetPixelDensity() const override;
        float GetDisplayScale() const override;
        Rect GetSafeArea() const override;
        void SetTitle(StringView title) override;
        bool IsHeadless() const override {
            return _headless;
        }
        std::expected<void, String> Simulate(const WindowSimulation& simulation) override;
        StringView GetConfiguredTitle() const override {
            return _configuredTitle;
        }

        /**
         * @brief Makes the window a headless one: call it before Initialize().
         *
         * Initialize() then asks SDL for its dummy video driver (no real window appears, nothing is shown) and takes
         * SDL_WINDOW_VULKAN out of the configured flags, which that driver cannot honour.
         */
        void SetHeadless(bool headless) {
            _headless = headless;
        }

        /**
         * @brief Gives a headless window the size and density it starts with instead of the config's size at density 1.
         *
         * Call it before Initialize(); a value that is not given stays the config's. Initialize() fails when the two together break
         * ValidateWindowSimulation (the output side of a config size times a density is only known there). A window that is not headless ignores it.
         */
        void SetHeadlessSimulation(Optional<WindowSize> size, Optional<float> density) {
            _startSize = size;
            _startDensity = density;
        }

        /**
         * @brief What `--hidden-window` and `--backend` ask of a real window: call it before Initialize().
         *
         * The flags of the config are changed by SDLUtils::ApplyStartOptions. The renderer reads the backend back (GetBackend()), so the
         * choice has one owner. A headless window ignores @p hidden.
         */
        void SetStartOptions(bool hidden, RenderBackendChoice backend) {
            _hidden = hidden;
            _backend = backend;
        }

        [[nodiscard]] RenderBackendChoice GetBackend() const {
            return _backend;
        }

        SDL_Window* GetSDLWindow() const {
            return _window;
        }

    private:
        void RenderPresent() const;

        /// A headless window is asked for its simulation; the SDL window of the dummy driver is in output pixels, not in window units
        [[nodiscard]] bool IsSimulated() const {
            return _headless && _window != nullptr;
        }

        SDL_Window* _window = nullptr;
        // The start size: the answer of GetWidth() / GetHeight() of a real window while there is no window or SDL cannot tell
        int _width = 0;
        int _height = 0;
        String _configuredTitle;
        bool _headless = false;
        bool _hidden = false;
        RenderBackendChoice _backend = RenderBackendChoice::Auto;
        /// What SetHeadlessSimulation() asked for; kept over Destroy(), like _headless
        Optional<WindowSize> _startSize;
        Optional<float> _startDensity;
        /// The state of a headless window while it exists: the answers of GetWidth(), GetHeight(), GetPixelDensity() and GetSafeArea()
        WindowSimulation _simulation;
    };

}  // namespace BECore
