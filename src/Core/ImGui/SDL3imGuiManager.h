#pragma once
#include <Core/ImGui/IimGuiManager.h>

namespace Core {
    class IMainWindow;
    class SDLMainWindow;

    class SDL3imGuiManager : public IimGuiManager {
    public:
        SDL3imGuiManager() = default;
        ~SDL3imGuiManager() override = default;

        bool Initialize() override;
        void Destroy() override;

        void NewFrame() override;
        void ProcessEvent(const SDL_Event* event) override;
        void Render() override;

        bool IsInitialized() const override {
            return _isInitialized;
        }

    private:

        SDL_Window* GetSDLWindow() const;
        SDL_Renderer* GetSDLRenderer() const;

         bool _isInitialized = false;
    };
}  // namespace Core