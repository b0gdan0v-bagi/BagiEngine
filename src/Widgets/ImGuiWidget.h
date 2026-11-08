#pragma once

#include <BECore/Widgets/IWidget.h>
#include <string>

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
        /// Shuts ImGui down when the widget dies: at a switch from Editor to Game mode and at program end (Application::Cleanup
        /// destroys the widgets before ApplicationCleanUpEvent, while the renderer's device is alive). The subscription to
        /// ApplicationCleanUpEvent goes with the widget, so Destroy() runs once.
        ~ImGuiWidget() override;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Draw() override;
        BE_FUNCTION void Update() override;

    private:
        static SDL_Window* GetSDLWindow();

        void Destroy();
        void OnNewFrame() const;
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
        std::string _iniFilePath;
    };

}  // namespace BECore
