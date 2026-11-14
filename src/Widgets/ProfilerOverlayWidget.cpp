#include "ProfilerOverlayWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextLayout.h>
#include <BECore/Text/TextRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/ProfilerOverlayWidget.gen.hpp>

namespace BECore {

    namespace {

        constexpr Color kBackground{0, 0, 0, 170};
        constexpr Color kText{235, 235, 235, 255};
        constexpr Color kBar{90, 200, 120, 255};
        constexpr Color kSlowBar{230, 90, 70, 255};
        constexpr Color kBudgetLine{255, 255, 255, 120};
        constexpr size_t kBars = 120;
        constexpr float kBarWidth = 3.0f;
        constexpr float kBarsHeight = 48.0f;
        constexpr float kBudgetLineHeight = 1.0f;
        constexpr float kBudgetsShown = 2.0f;  // the bar area spans two frame budgets

    }  // namespace

    bool ProfilerOverlayWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<SDLEvents::SDLEventWrapper, &ProfilerOverlayWidget::OnSDLEvent>(this);
        return true;
    }

    void ProfilerOverlayWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) {
        const SDL_KeyboardEvent& key = event.event.key;
        if (kProfilerViewsBuilt && event.event.type == SDL_EVENT_KEY_DOWN && IsProfilerToggle(key.key, key.mod, key.repeat)) {
            _visible = !_visible;
        }
    }

    void ProfilerOverlayWidget::Update() {}

    void ProfilerOverlayWidget::Draw() {}

    void ProfilerOverlayWidget::DrawScreen() {
        BE_PROFILE_SCOPE("ProfilerOverlay::DrawScreen");
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
                LOG_ERROR("ProfilerOverlayWidget: cannot load the font '{}', the overlay shows only the bars"_format(path.ToStringView()));
                _font.Reset();
            }
        }
        return _font ? _font.Get() : nullptr;
    }

    void ProfilerOverlayWidget::DrawOverlay(IRenderer& renderer) {
        const FrameStats* last = CoreManager::GetFrameStats().GetLast();
        if (last == nullptr) {
            return;
        }
        const ScopedRenderCountersPause pause(renderer);
        // Output pixels: the scene leaves its own view transform behind
        const ViewTransform sceneView = renderer.GetViewTransform();
        renderer.SetViewTransform(ViewTransform{});

        String text;
        for (const String& line : FormatOverlayLines(*last, AllocTracker::GetTotals(), AllocTracker::kEnabled)) {
            text.append(line);
            text.push_back('\n');
        }
        TextLayoutOptions options;
        options.fontSize = _fontSize;
        options.wrap = false;
        Font* font = ResolveFont();
        TextLayoutResult layout;
        if (font != nullptr) {
            layout = TextLayout::Layout(*font, text, options);
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
        const eastl::vector<FrameStats> history = CoreManager::GetFrameStats().GetHistory(kBars);
        float x = 2.0f * _margin + (kBars - history.size()) * kBarWidth;
        for (const FrameStats& frame : history) {
            const float barHeight = std::min(kBarsHeight, static_cast<float>(frame.frameMs) / msPerPixel);
            renderer.DrawFilledRect(x, barsBottom - barHeight, kBarWidth - 1.0f, barHeight, frame.frameMs > _budgetMs ? kSlowBar : kBar);
            x += kBarWidth;
        }
        renderer.DrawFilledRect(2.0f * _margin, barsBottom - _budgetMs / msPerPixel, kBars * kBarWidth, kBudgetLineHeight, kBudgetLine);

        renderer.SetViewTransform(sceneView);
    }

}  // namespace BECore
