#pragma once

#include <Core/Widgets/IWidget.h>

// Forward declarations for SDL types
struct SDL_Window;
struct SDL_Renderer;

namespace Core {
namespace SDLEvents {
    struct SDLEventWrapper;
}

    class ImGuiWidget : public IWidget {
    public:
        ImGuiWidget() = default;
        ~ImGuiWidget() override = default;

        bool Initialize(const XmlNode& node) override;
        void Draw() override;
        void Update() override;

    private:
        static SDL_Window* GetSDLWindow();
        static SDL_Renderer* GetSDLRenderer();

        void Destroy();
        void OnNewFrame() const;
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
    };

}  // namespace Core

