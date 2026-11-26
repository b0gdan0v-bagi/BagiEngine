#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <Stress/StressSettings.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace Rml {

    class Element;
    class ElementDocument;

}  // namespace Rml

namespace BECore::Stress {

    /// The named frame counters of the scene (`frame_stats` answers them): what a controller sees of it
    struct RmlUiCounters {
        static constexpr auto documents = "Stress rmlui documents"_intern;
        static constexpr auto textures = "Stress rmlui textures"_intern;  // the render interface's, so every context's glyph pages and images
        static constexpr auto reloads = "Stress rmlui reloads"_intern;
        static constexpr auto scrollX = "Stress rmlui scroll x"_intern;
        static constexpr auto scrollY = "Stress rmlui scroll y"_intern;
    };

    /**
     * @brief The scene `StressRmlUi`: `StressState::GetCount()` RmlUi documents, each its own `Rml::Context` with text at `kRmlUiSizes` font sizes, and
     *        one scrollable panel loaded from a file.
     *
     * The documents are `RmlUiDocumentComponent` children of the node `Docs` (a child of this node), made when `StressState::Version()` moved: a bigger count
     * appends at the tail, a smaller one removes from it, and a change of the count or the language gives every document its markup again (the cell
     * depends on the count). The panel node comes after `Docs`, so it is drawn and takes input on top; a wheel over a generated document reaches it
     * because their body says `pointer-events: none`. `StressGame::GetRmlUiReloadRequests()` against the number served here makes the panel `Reload()`
     * and the documents `SetMarkup()` again, once per request. `RmlUiCounters` are set every frame.
     *
     * Only with `STRESS_WITH_RMLUI`; without it the scene file still loads and the component does nothing. The header names no RmlUi header, so a
     * build without the host compiles it unchanged. Does nothing while the game is not the active `StressGame`.
     */
    class StressRmlUiComponent : public IComponent {
        BE_CLASS(StressRmlUiComponent, CATEGORY = logic)
    public:
        StressRmlUiComponent();
        ~StressRmlUiComponent() override;

        /// It has no OnDraw(): its panels draw through the RmlUi host
        bool HasOnDraw() const override {
            return false;
        }

        void OnAttached() override;

        /// The file of the panel, a virtual path; call it before the first update (the panel is made then). Empty (the default) is `assets/ui/stress_rmlui.rml`
        void SetPanelSource(PoolString source) {
            _panelSource = source;
        }

        /// The documents made so far (a test counts them)
        size_t GetDocumentCount() const {
            return _documents.size();
        }

    private:
        static constexpr uint32_t kNothingBuilt = std::numeric_limits<uint32_t>::max();  // above kMaxCount: the first update always builds

        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// The nodes `Docs` and the panel, once (at the first update, so the renderer and the runtime are up)
        void EnsureNodes();
        /// Brings the documents to @p count (appends or removes from the tail) and gives every one its markup
        void Rebuild(uint32_t count, StressLanguage language);
        void Remark(uint32_t count, StressLanguage language);
        /// Loads the panel and the documents again for the requests not served yet
        void ServeReloads(uint32_t requests, uint32_t count, StressLanguage language);
        /// The element the scroll counters read; looked up again when the panel's document is another one
        void TrackScroller();
        void SetCounters() const;

        Vector<IntrusivePtr<SceneNode>> _documents;
        IntrusivePtr<SceneNode> _docsNode;
        IntrusivePtr<SceneNode> _panelNode;
        PoolString _panelSource;
        Rml::ElementDocument* _trackedDocument = nullptr;  // only compared; the pointer of the document the scroller below belongs to
        Rml::Element* _scroller = nullptr;
        uint64_t _builtVersion = 0;  // the StressState version the documents were last brought to; 0 is never a version
        uint32_t _builtCount = kNothingBuilt;
        uint32_t _reloadsServed = 0;
        StressLanguage _builtLanguage = StressLanguage::En;
    };

}  // namespace BECore::Stress
