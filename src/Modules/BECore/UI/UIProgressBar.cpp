#include "UIProgressBar.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Generated/UIProgressBar.gen.hpp>

namespace BECore {

    float UIProgressBar::GetFraction() const {
        if (bind.Empty()) {
            return 0.0f;
        }
        const auto value = UIText::ParseFiniteFloat(CoreManager::GetUIVars().Get(bind));
        return value ? UIText::FractionInRange(*value, minValue, maxValue) : 0.0f;
    }

    Color UIProgressBar::GetCurrentTrack() const {
        return trackColor.a > 0 ? Color(trackColor) : kTrackFallback;
    }

    Color UIProgressBar::GetCurrentFill() const {
        return fillColor.a > 0 ? Color(fillColor) : kFillFallback;
    }

    void UIProgressBar::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        if (rect.IsEmpty()) {
            return;
        }
        DrawPart(renderer, rect, GetCurrentTrack(), partShape);

        const float filled = rect.w * GetFraction();
        if (filled > 0.0f) {
            DrawPart(renderer, {rect.x, rect.y, filled, rect.h}, GetCurrentFill(), partShape);
        }
    }

}  // namespace BECore
