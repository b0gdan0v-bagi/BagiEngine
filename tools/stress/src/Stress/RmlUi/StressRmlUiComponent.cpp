#include "StressRmlUiComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/SceneEvents.h>
#include <Generated/StressRmlUiComponent.gen.hpp>
#include <Stress/RmlUi/RmlUiField.h>
#include <Stress/StressGame.h>
#include <cmath>

#ifdef STRESS_WITH_RMLUI
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <RmlUiHost/RmlUiRuntime.h>
#endif

namespace BECore::Stress {

    StressRmlUiComponent::StressRmlUiComponent() = default;
    StressRmlUiComponent::~StressRmlUiComponent() = default;

#ifdef STRESS_WITH_RMLUI

    namespace {

        struct Ids {
            static constexpr auto docs = "Docs"_intern;
            static constexpr auto document = "Document"_intern;
            static constexpr auto panel = "Panel"_intern;
            static constexpr auto defaultPanel = "assets/ui/stress_rmlui.rml"_intern;
        };

        /// The element of the panel that scrolls (`stress_rmlui.rml`)
        constexpr const char* kScrollerId = "scroller";

        /// The reference size of a document: the scene's view, so a grid cell of `RmlUiField` is the same number of `dp`
        constexpr float kReferenceWidth = 1920.0f;
        constexpr float kReferenceHeight = 1080.0f;

        void Configure(RmlUiDocumentComponent& document) {
            document._space = CanvasSpace::Screen;
            document._scaleMode = CanvasScaleMode::ScaleWithScreen;
            document._referenceWidth = kReferenceWidth;
            document._referenceHeight = kReferenceHeight;
        }

        RmlUiDocumentComponent* DocumentOf(const IntrusivePtr<SceneNode>& node) {
            return node->GetComponent<RmlUiDocumentComponent>().Get();
        }

    }  // namespace

    void StressRmlUiComponent::OnAttached() {
        // Nothing to do at ApplicationCleanUpEvent: RmlUiRuntime waits for the GPU and shuts RmlUi down first (it subscribed before any context
        // existed), and a context that outlives it finds the runtime down and removes nothing
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressRmlUiComponent::OnUpdate>(this);
    }

    void StressRmlUiComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        StressGame* game = StressGame::GetActive();
        if (game == nullptr) {
            return;
        }
        EnsureNodes();
        const StressState& state = game->GetState();
        if (state.Version() != _builtVersion) {
            _builtVersion = state.Version();
            if (state.GetCount() != _builtCount || state.GetLanguage() != _builtLanguage) {
                Rebuild(state.GetCount(), state.GetLanguage());
            }
        }
        if (game->GetRmlUiReloadRequests() != _reloadsServed) {
            ServeReloads(game->GetRmlUiReloadRequests(), state.GetCount(), state.GetLanguage());
        }
        TrackScroller();
        SetCounters();
    }

    void StressRmlUiComponent::EnsureNodes() {
        if (_panelNode) {
            return;
        }
        // `Docs` first: a later sibling is drawn and takes input on top, and the panel must be above the documents
        _docsNode = GetNode().AddChild(Ids::docs);
        _panelNode = GetNode().AddChild(Ids::panel);
        auto panel = New<RmlUiDocumentComponent>();
        Configure(*panel);
        panel->_source = _panelSource.Empty() ? PoolString(Ids::defaultPanel) : _panelSource;
        _panelNode->AddComponent(panel);
    }

    void StressRmlUiComponent::Rebuild(uint32_t count, StressLanguage language) {
        const size_t target = count;
        while (_documents.size() > target) {
            _docsNode->RemoveChild(_documents.back().Get());
            _documents.pop_back();
        }
        _documents.reserve(target);
        while (_documents.size() < target) {
            IntrusivePtr<SceneNode> node = _docsNode->AddChild(Ids::document);
            auto document = New<RmlUiDocumentComponent>();
            Configure(*document);
            node->AddComponent(document);
            _documents.push_back(std::move(node));
        }
        Remark(count, language);
        _builtCount = count;
        _builtLanguage = language;
    }

    void StressRmlUiComponent::Remark(uint32_t count, StressLanguage language) {
        const FieldView view{GetScene().GetViewWidth(), GetScene().GetViewHeight()};
        for (size_t i = 0; i < _documents.size(); ++i) {
            if (!DocumentOf(_documents[i])->SetMarkup(MakeDocumentMarkup(static_cast<uint32_t>(i), count, language, view))) {
                LOG_ERROR("StressRmlUiComponent: RmlUi refused the markup of document {}", i);
            }
        }
    }

    void StressRmlUiComponent::ServeReloads(uint32_t requests, uint32_t count, StressLanguage language) {
        _reloadsServed = requests;
        if (!DocumentOf(_panelNode)->Reload()) {
            LOG_ERROR("StressRmlUiComponent: the panel could not be loaded again");
        }
        _trackedDocument = nullptr;  // the scroller of the old document is gone
        Remark(count, language);
    }

    void StressRmlUiComponent::TrackScroller() {
        Rml::ElementDocument* document = DocumentOf(_panelNode)->GetDocument();
        if (document == _trackedDocument) {
            return;
        }
        _trackedDocument = document;
        _scroller = document != nullptr ? document->GetElementById(kScrollerId) : nullptr;
    }

    void StressRmlUiComponent::SetCounters() const {
        FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
        frameStats.SetCounter(RmlUiCounters::documents, static_cast<double>(_documents.size()));
        frameStats.SetCounter(RmlUiCounters::textures, static_cast<double>(RmlUiRuntime::GetInstance().GetRenderInterface().GetTextureCount()));
        frameStats.SetCounter(RmlUiCounters::reloads, _reloadsServed);
        // Whole pixels: a controller compares them, and a fraction of RmlUi's smooth scroll is noise
        frameStats.SetCounter(RmlUiCounters::scrollX, _scroller != nullptr ? std::floor(_scroller->GetScrollLeft()) : 0.0);
        frameStats.SetCounter(RmlUiCounters::scrollY, _scroller != nullptr ? std::floor(_scroller->GetScrollTop()) : 0.0);
    }

#else

    void StressRmlUiComponent::OnAttached() {}

#endif

}  // namespace BECore::Stress
