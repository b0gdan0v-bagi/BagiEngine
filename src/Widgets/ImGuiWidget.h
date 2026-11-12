#pragma once

#include <BECore/Widgets/IWidget.h>
#include <Widgets/ImGuiFonts.h>

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
        void OnNewFrame();
        /// Sets style.FontScaleDpi from the window when its scale differs from the one applied last
        void ApplyWindowScale();
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
        std::string _iniFilePath;
        /// ImGui reads these bytes while its atlas lives: they are released after the context
        ImGuiFontBuffers _fontBuffers;
        ImGuiFonts _fonts;
        float _appliedWindowScale = 0.0f;
    };

}  // namespace BECore
