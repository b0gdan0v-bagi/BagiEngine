#include "UILabel.h"

#include <Generated/UILabel.gen.hpp>

namespace BECore {

    UITextParams UILabel::MakeParams() const {
        const eastl::string& source = GetSourceText();
        UITextParams params;
        params.text = eastl::string_view(source.data(), source.size());
        params.font = font;
        params.fontFamily = fontFamily;
        params.fontWeight = fontWeight;
        params.fontSize = active.fontSize;
        params.align = align;
        params.verticalAlign = verticalAlign;
        params.overflow = overflow;
        params.wrap = wrap;
        params.lineSpacing = lineSpacing;
        params.minFontSize = minFontSize;
        params.effects = textEffects;
        return params;
    }

    eastl::string_view UILabel::GetDisplayText() const {
        return _painter.GetDisplayText(MakeParams().text);
    }

    float UILabel::GetFontSize() const {
        return UITextPainter::EffectiveFontSize(active.fontSize);
    }

    PoolString UILabel::GetResolvedFontPath() const {
        return _painter.GetResolvedFontPath(MakeParams());
    }

    void UILabel::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        DrawText(renderer);
    }

    void UILabel::DrawText(IRenderer& renderer) {
        DrawText(renderer, rect, textColor);
    }

    void UILabel::DrawText(IRenderer& renderer, const Rect& area, const Color& color) {
        _painter.Draw(renderer, MakeParams(), area, color, textColor);
    }

    Vec2 UILabel::MeasureText(float maxWidth, float scale) const {
        return _painter.Measure(MakeParams(), maxWidth, scale);
    }

    UIContentFit UILabel::CheckContent(float width, float height, float scale) const {
        return _painter.Check(MakeParams(), width, height, scale);
    }

}  // namespace BECore
