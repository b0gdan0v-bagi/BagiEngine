#pragma once

#include <BECore/Widgets/IWidget.h>

struct SDL_Window;

namespace BECore {
    class IDeserializer;

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    class ImGuiWidget : public IWidget {
        BE_CLASS(ImGuiWidget)
    public:
        ImGuiWidget() = default;
        ~ImGuiWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Draw() override;
        BE_FUNCTION void Update() override;

    private:
        static SDL_Window* GetSDLWindow();

        void Destroy();
        void OnNewFrame() const;
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
    };

}  // namespace BECore
