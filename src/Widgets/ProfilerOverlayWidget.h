#pragma once

#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Profiler/Ring/ZoneStats.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/TextLayout.h>
#include <BECore/Text/TextLayoutCache.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;
    class IRenderer;
    struct FrameStats;

    /// The one zone of the overlay's own work that the frame ring records (its inner zones are paused, Ring/RingPause.h), and the name the
    /// list of top zones leaves out
    inline constexpr char kProfilerOverlayZone[] = "Profiler overlay";

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
     *
     * In a build with the profiler the frame ring runs only while the overlay is visible (and is switched off when it is hidden or
     * destroyed). Every kZoneRefreshFrames frames the overlay takes one snapshot of the ring and keeps the zone statistics of the last
     * kZoneWindowFrames frames; the frames between draw the kept rows. Its own cost stays out of those numbers: the draw is one zone,
     * kProfilerOverlayZone, and the zones inside it are not recorded.
     */
    class ProfilerOverlayWidget : public IWidget {
        BE_CLASS(ProfilerOverlayWidget)
    public:
        static constexpr uint32_t kZoneRefreshFrames = 30;  ///< frames between two snapshots of the ring
        static constexpr uint32_t kZoneWindowFrames = 60;   ///< the ring frames the top zones are taken over
        static constexpr size_t kTopZoneCount = 6;

        ProfilerOverlayWidget() = default;
        ~ProfilerOverlayWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void DrawScreen() override;
        void Draw() override;

        [[nodiscard]] bool IsVisible() const {
            return _visible;
        }
        void SetVisible(bool visible) {
            _visible = visible;
            SyncRing();
        }

        /// Draws the overlay into @p renderer now (DrawScreen does it with the active renderer when visible)
        void DrawOverlay(IRenderer& renderer);

        /// Takes a snapshot of the ring and recomputes the zone rows now (DrawOverlay does it every kZoneRefreshFrames frames); without the
        /// profiler there are no rows
        void RefreshZones();

        /// The lines of the overlay text: the last frame, then the top zones of the kept rows (in a build with the profiler)
        void EmitLines(const FrameStats& last, FunctionRef<void(StringView)> emit) const;

    private:
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);
        /// Makes the ring's state follow the visibility
        void SyncRing();
        Font* ResolveFont();

        BE_REFLECT_FIELD bool _visible = false;              ///< shown from the start
        BE_REFLECT_FIELD float _fontSize = 14.0f;            ///< output pixels
        BE_REFLECT_FIELD float _margin = 8.0f;               ///< from the top-left corner of the output and around the text, output pixels
        BE_REFLECT_FIELD float _budgetMs = 1000.0f / 60.0f;  ///< the frame time drawn as a full bar's guide line

        ResourceHandle<Font> _font;
        bool _fontTried = false;
        TextLayoutCache _layoutCache;  // the text changes with every frame's numbers, but the result keeps its capacity
        TextLayoutResult _noLayout;    // what is drawn around when there is no font: no text

        bool _ringRequested = false;
        uint32_t _framesSinceRefresh = kZoneRefreshFrames;  // a fresh ring is read at the first draw
        uint32_t _windowFrames = 0;                         // the ring frames _rows cover
        RingSnapshot _snapshot;                             // refilled in place, so it keeps its capacity
        Vector<ZoneStatRow> _rows;
    };

}  // namespace BECore
