#pragma once

#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Text/Font.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;
    class IRenderer;

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /**
     * @brief The Game-mode profiler overlay: the last frame's statistics and a bar per frame of the recent frame times, drawn by the
     * engine (a translucent rect and text of the default font), so it needs no ImGui.
     *
     * Ctrl+Shift+P shows and hides it (Profiler/ProfilerToggle.h), read from the raw SDL event. Listed last in GameWidgetsConfig.xml,
     * so it draws over the scene in DrawScreen(). Its draws are left out of the frame statistics (ScopedRenderCountersPause). In a
     * plain Release build (no editor, no profiler) the chord is ignored and nothing is drawn.
     */
    class ProfilerOverlayWidget : public IWidget {
        BE_CLASS(ProfilerOverlayWidget)
    public:
        ProfilerOverlayWidget() = default;
        ~ProfilerOverlayWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void DrawScreen() override;
        BE_FUNCTION void Draw() override;

        [[nodiscard]] bool IsVisible() const {
            return _visible;
        }
        void SetVisible(bool visible) {
            _visible = visible;
        }

        /// Draws the overlay into @p renderer now (DrawScreen does it with the active renderer when visible)
        void DrawOverlay(IRenderer& renderer);

    private:
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);
        Font* ResolveFont();

        BE_REFLECT_FIELD bool _visible = false;              ///< shown from the start
        BE_REFLECT_FIELD float _fontSize = 14.0f;            ///< output pixels
        BE_REFLECT_FIELD float _margin = 8.0f;               ///< from the top-left corner of the output and around the text, output pixels
        BE_REFLECT_FIELD float _budgetMs = 1000.0f / 60.0f;  ///< the frame time drawn as a full bar's guide line

        ResourceHandle<Font> _font;
        bool _fontTried = false;
    };

}  // namespace BECore
