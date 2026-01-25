#pragma once

#include <BECore/Widgets/IWidget.h>

// Forward declarations for SDL types
struct SDL_Window;
struct SDL_Renderer;

namespace BECore {
    class IArchive;

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    class ImGuiWidget : public IWidget {
        BE_CLASS(ImGuiWidget)
    public:
        ImGuiWidget() = default;
        ~ImGuiWidget() override = default;

        BE_FUNCTION bool Initialize(IArchive& archive) override;
        BE_FUNCTION void Draw() override;
        BE_FUNCTION void Update() override;

    private:
        static SDL_Window* GetSDLWindow();
        static SDL_Renderer* GetSDLRenderer();

        void Destroy();
        void OnNewFrame() const;
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
    };

}  // namespace BECore
