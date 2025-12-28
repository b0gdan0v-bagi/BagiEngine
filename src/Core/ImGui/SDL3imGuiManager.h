#pragma once
#include <Core/ImGui/IimGuiManager.h>

namespace Core {
    struct SDLEventWrapper;
    class IMainWindow;
    class SDLMainWindow;
    class EventManager;

    class SDL3imGuiManager : public IimGuiManager {
    public:
        SDL3imGuiManager() = default;
        ~SDL3imGuiManager() override = default;

        bool Initialize() override;
        void Destroy() override;

        void NewFrame() override;
        void Render() override;

        bool IsInitialized() const override {
            return _isInitialized;
        }

    private:
        static SDL_Window* GetSDLWindow();
        static SDL_Renderer* GetSDLRenderer();

        void OnSDLEvent(const SDLEventWrapper& event) const;

        bool _isInitialized = false;
        EventManager* _eventManager = nullptr;
    };
}  // namespace Core