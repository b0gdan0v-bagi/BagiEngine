#include "ProfilerOverlayWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Profiler/Ring/RingPause.h>
#include <BECore/Profiler/Ring/RingTraceExport.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextLayout.h>
#include <BECore/Text/TextRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLWebDownload.h>
#include <Generated/ProfilerOverlayWidget.gen.hpp>

namespace BECore {

    namespace {

        constexpr Color kBackground{0, 0, 0, 170};
        constexpr Color kText{235, 235, 235, 255};
        constexpr Color kBar{90, 200, 120, 255};
        constexpr Color kSlowBar{230, 90, 70, 255};
        constexpr Color kBudgetLine{255, 255, 255, 120};
        constexpr size_t kBars = 120;
        constexpr size_t kTextCapacity = 2048;  // the overlay's lines, so the text grows once
        constexpr float kBarWidth = 3.0f;
        constexpr float kBarsHeight = 48.0f;
        constexpr float kBudgetLineHeight = 1.0f;
        constexpr float kBudgetsShown = 2.0f;
        std::expected<void, String> SaveTrace(StringView fileName, const String& json) {
#ifdef __EMSCRIPTEN__
            return WebDownloadStart(fileName, json, "application/json");
#else
            return WriteTextFile(fileName, json);
#endif
        }

    }  // namespace

    bool ProfilerOverlayWidget::SaveTraceTarget::ContainsPoint(float outputX, float outputY) const {
        const Rect& rect = _owner._saveRect;
        return outputX >= rect.x && outputX < rect.x + rect.w && outputY >= rect.y && outputY < rect.y + rect.h;
    }

    bool ProfilerOverlayWidget::SaveTraceTarget::OnMouseButtonDown(const SDL_MouseButtonEvent& event) {
        if (event.button != SDL_BUTTON_LEFT) {
            return false;
        }
        _owner.ExportTrace();
        return true;
    }

    ProfilerOverlayWidget::~ProfilerOverlayWidget() {
        // A widget destroyed by a mode switch while visible must not leave the ring recording
        if (_ringRequested) {
            Profiler::SetRingEnabled(false);
            CoreManager::GetUIInput().Unregister(_saveTarget);
        }
    }

    bool ProfilerOverlayWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<SDLEvents::SDLEventWrapper, &ProfilerOverlayWidget::OnSDLEvent>(this);
        SyncRing();  // shown from the start
        return true;
    }

    void ProfilerOverlayWidget::SyncRing() {
        const bool wanted = kProfilerViewsBuilt && BE_PROFILER != 0 && _visible;
        if (wanted == _ringRequested) {
            return;
        }
        Profiler::SetRingEnabled(wanted);
        _ringRequested = wanted;
        if (wanted) {
            CoreManager::GetUIInput().Register(_saveTarget, kInputOrder);
            _framesSinceRefresh = kZoneRefreshFrames;
            _rows.clear();
            _windowFrames = 0;
        } else {
            CoreManager::GetUIInput().Unregister(_saveTarget);
            _saveRect = Rect{};
        }
    }

    void ProfilerOverlayWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) {
        const SDL_KeyboardEvent& key = event.event.key;
        if (!kProfilerViewsBuilt || event.event.type != SDL_EVENT_KEY_DOWN) {
            return;
        }
        if (IsProfilerToggle(key.key, key.mod, key.repeat)) {
            SetVisible(!_visible);
        } else if (IsProfilerExportChord(key.key, key.mod, key.repeat)) {
            ExportTrace();
        }
    }

    bool ProfilerOverlayWidget::ExportTrace() {
        if constexpr (BE_PROFILER == 0) {
            return false;
        } else {
            if (!_visible) {
                return false;
            }
            // The same exclusion as the draw: one block on the timeline, and the work inside (the JSON, the file) is not recorded
            const ScopedAllocCountersPause allocPause;
            BE_PROFILE_SCOPE(kProfilerOverlayZone);
            const ScopedRingPause ringPause;
            const RingSnapshot snapshot = Profiler::GetFrameRing().Snapshot();
            const String json = WriteRingTraceJson(snapshot, kExportSeconds);
            const std::expected<void, String> saved = _exportSink ? _exportSink(kProfilerTraceFileName, json) : SaveTrace(kProfilerTraceFileName, json);
            _status = saved ? Format("Saved {} ({} frames)", kProfilerTraceFileName, CountRingTraceFrames(snapshot, kExportSeconds)) : Format("Save failed: {}", saved.error());
            _statusFramesLeft = kStatusFrames;
            return saved.has_value();
        }
    }

    void ProfilerOverlayWidget::Update() {}

    void ProfilerOverlayWidget::Draw() {}

    void ProfilerOverlayWidget::DrawScreen() {
        const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();
        if (kProfilerViewsBuilt && _visible && renderer) {
            DrawOverlay(*renderer);
        }
    }

    Font* ProfilerOverlayWidget::ResolveFont() {
        if (!_fontTried) {
            // The default face of TextConfig.xml, as a text without a font asks for it (UITextPainter)
            _fontTried = true;
            const PoolString path = CoreManager::GetFontAtlasLibrary().ResolveFontPath(FontRequest{});
            _font = CoreManager::GetResourceManager().Load<Font>(path.ToStringView());
            if (!_font || _font->GetState() != ResourceState::Loaded) {
                LOG_ERROR("ProfilerOverlayWidget: cannot load the font '{}', the overlay shows only the bars", path.ToStringView());
                _font.Reset();
            }
        }
        return _font ? _font.Get() : nullptr;
    }

    void ProfilerOverlayWidget::RefreshZones() {
        _framesSinceRefresh = 0;
        if constexpr (BE_PROFILER != 0) {
            Profiler::GetFrameRing().Snapshot(_snapshot);
            if (_snapshot.frames.empty()) {
                _rows.clear();
                _windowFrames = 0;
                return;
            }
            const uint64_t newest = _snapshot.frames.back().index;
            const uint64_t first = newest >= kZoneWindowFrames - 1 ? newest - (kZoneWindowFrames - 1) : 0;
            ComputeZoneStats(_snapshot, first, newest, _rows);
            _windowFrames = static_cast<uint32_t>(eastl::min<size_t>(_snapshot.frames.size(), kZoneWindowFrames));
        }
    }

    void ProfilerOverlayWidget::EmitLines(const FrameStats& last, FunctionRef<void(StringView)> emit) const {
        FormatOverlayLinesTo(last, AllocTracker::GetTotals(), AllocTracker::kEnabled, emit);
        if constexpr (BE_PROFILER != 0) {
            char title[kOverlayLineCapacity];
            const FormatResult written = FormatToN(title, kOverlayLineCapacity, "Top zones by self time (per frame, last {})", kZoneWindowFrames);
            emit(StringView(title, eastl::min(written.size, kOverlayLineCapacity)));
            FormatTopZoneLinesTo(_rows, _windowFrames, kTopZoneCount, kProfilerOverlayZone, emit);
            emit(kSaveTraceLine);
            if (_statusFramesLeft > 0) {
                emit(StringView(_status.data(), eastl::min(_status.size(), kOverlayLineCapacity)));
            }
        }
    }

    void ProfilerOverlayWidget::DrawOverlay(IRenderer& renderer) {
        const FrameStats* last = CoreManager::GetFrameStats().GetLast();
        if (last == nullptr) {
            return;
        }
        // The overlay's own draws and allocations (text, layout, lines) are not what the frame statistics should show
        const ScopedAllocCountersPause allocPause;
        const ScopedRenderCountersPause pause(renderer);
        // The one zone of the overlay's cost that the ring records; what runs inside it (text layout, the glyph atlas, the zone statistics)
        // is paused, so it is a block on the timeline and not many small zones in the numbers the overlay prints
        BE_PROFILE_SCOPE(kProfilerOverlayZone);
        const ScopedRingPause ringPause;
        if (_ringRequested && ++_framesSinceRefresh >= kZoneRefreshFrames) {
            RefreshZones();
        }
        // Output pixels: the scene leaves its own view transform behind
        const ViewTransform sceneView = renderer.GetViewTransform();
        renderer.SetViewTransform(ViewTransform{});

        const ScratchScope scratch;
        ArenaString text;
        text.reserve(kTextCapacity);
        size_t lineCount = 0;
        size_t saveLine = 0;
        EmitLines(*last, [&](StringView line) {
            saveLine = line == kSaveTraceLine ? lineCount : saveLine;
            ++lineCount;
            text.append(line.data(), line.size());
            text.push_back('\n');
        });
        _statusFramesLeft -= _statusFramesLeft > 0 ? 1 : 0;
        TextLayoutOptions options;
        options.fontSize = _fontSize;
        options.wrap = false;
        Font* font = ResolveFont();
        // _layoutCache is only ever given _font, which ResolveFont never changes after the first call, so it needs no Reset
        const TextLayoutResult& layout = font != nullptr ? _layoutCache.Get(*font, StringView(text.data(), text.size()), options) : _noLayout;

        // The click target of the "Save trace" line: the lines are a line height apart
        _saveRect = Rect{};
        if (_ringRequested && font != nullptr && saveLine < layout.lines.size()) {
            _saveRect = Rect{2.0f * _margin, 2.0f * _margin + static_cast<float>(saveLine) * layout.lineHeight, layout.width, layout.lineHeight};
        }

        const float width = std::max(layout.width, kBars * kBarWidth) + 2.0f * _margin;
        const float height = layout.height + kBarsHeight + 3.0f * _margin;
        renderer.DrawFilledRect(_margin, _margin, width, height, kBackground);

        if (font != nullptr && !layout.glyphs.empty()) {
            if (auto atlas = CoreManager::GetFontAtlasLibrary().Get(renderer, IntrusivePtr<Font>(font), FontAtlasLibrary::QuantizeSize(layout.fontSize))) {
                TextRenderer::Draw(renderer, *atlas, layout, 2.0f * _margin, 2.0f * _margin, kText);
            }
        }

        // One bar per frame, the newest on the right; the line is the frame budget
        const float barsBottom = 2.0f * _margin + layout.height + _margin + kBarsHeight;
        const float msPerPixel = (_budgetMs * kBudgetsShown) / kBarsHeight;
        const FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
        float x = 2.0f * _margin + (kBars - frameStats.RecentCount(kBars)) * kBarWidth;
        frameStats.ForEachRecent(kBars, [&](const FrameStats& frame) {
            const float barHeight = std::min(kBarsHeight, static_cast<float>(frame.frameMs) / msPerPixel);
            renderer.DrawFilledRect(x, barsBottom - barHeight, kBarWidth - 1.0f, barHeight, frame.frameMs > _budgetMs ? kSlowBar : kBar);
            x += kBarWidth;
        });
        renderer.DrawFilledRect(2.0f * _margin, barsBottom - _budgetMs / msPerPixel, kBars * kBarWidth, kBudgetLineHeight, kBudgetLine);

        renderer.SetViewTransform(sceneView);
    }

}  // namespace BECore
