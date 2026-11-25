#pragma once

#include <BECore/File/Path.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/ImGuiFonts.h>

struct SDL_Window;

namespace BECore {
    class IDeserializer;

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /// ImGui's frame time never goes below this: it asserts `DeltaTime > 0` after the first frame, and a frame of zero seconds (a `screenshot`, `imgui_find`) is real
    inline constexpr float kMinImGuiFrameSeconds = 1.0e-6f;

    /**
     * @brief The `io.DeltaTime` of the next ImGui frame.
     *
     * The SDL backend measures the wall clock, which on the manual clock makes a double click, a tooltip delay, the caret blink and a window fade depend on the
     * machine and on how fast a client sends requests. With the manual clock the frame lasts what the clock says (@p clockDelta, the `step`'s `dt`), at least
     * `kMinImGuiFrameSeconds`; with the real clock the backend's @p backendDelta is kept.
     */
    [[nodiscard]] float ImGuiFrameSeconds(bool manualClock, double clockDelta, float backendDelta);

    class ImGuiWidget : public IWidget {
        BE_CLASS(ImGuiWidget)
    public:
        ImGuiWidget() = default;
        /// Shuts ImGui down when the widget dies: at a switch from Editor to Game mode and at program end (Application::Cleanup
        /// destroys the widgets before ApplicationCleanUpEvent, while the renderer's device is alive). The subscription to
        /// ApplicationCleanUpEvent goes with the widget, so Destroy() runs once.
        ~ImGuiWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Draw() override;
        void Update() override;

    private:
        static SDL_Window* GetSDLWindow();

        void Destroy();
        void OnNewFrame();
        /// Sets style.FontScaleDpi and the theme (its sizes grow with the scale) when the window's scale differs from the one applied last
        void ApplyWindowScale();
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const;

        bool _isInitialized = false;
        Path _iniFilePath;
        /// ImGui reads these bytes while its atlas lives: they are released after the context
        ImGuiFontBuffers _fontBuffers;
        ImGuiFonts _fonts;
        float _appliedWindowScale = 0.0f;
    };

}  // namespace BECore
