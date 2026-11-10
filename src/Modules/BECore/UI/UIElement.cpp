#include "UIElement.h"

#include <BECore/Renderer/IRenderer.h>
#include <Generated/UIElement.gen.hpp>

namespace BECore {

    IntrusivePtr<UIElement> UIElement::AddChild(PoolString childName) {
        auto child = New<UIElement>();
        child->name = childName;
        children.push_back(child);
        return child;
    }

    UIElement* UIElement::FindByName(PoolString searched) {
        if (name == searched) {
            return this;
        }
        for (const auto& child : children) {
            if (UIElement* found = child->FindByName(searched)) {
                return found;
            }
        }
        return nullptr;
    }

    void UIElement::DrawBox(IRenderer& renderer, const UIBoxStyle& boxStyle, const Color& fill, const Color& gradientEnd) {
        DrawBoxIn(renderer, rect, boxStyle, fill, gradientEnd);
    }

    void UIElement::DrawBoxIn(IRenderer& renderer, const Rect& area, const UIBoxStyle& boxStyle, const Color& fill, const Color& gradientEnd) {
        if (area.IsEmpty()) {
            return;
        }
        if (!boxStyle.HasEffect()) {
            if (fill.a > 0) {
                renderer.DrawFilledRect(area.x, area.y, area.w, area.h, fill);
            }
            return;
        }

        const float pixelsPerUnit = renderer.GetViewTransform().scale;
        _boxVertices.clear();
        _boxIndices.clear();
        // Each appender draws nothing for a colour with no alpha, so the order is all that is decided here
        UIBoxMesh::AppendShadow(_boxVertices, _boxIndices, area, boxStyle.cornerRadius, boxStyle.shadow, pixelsPerUnit);
        UIBoxMesh::AppendShadow(_boxVertices, _boxIndices, area, boxStyle.cornerRadius, boxStyle.glow, pixelsPerUnit);
        UIBoxMesh::AppendFill(_boxVertices, _boxIndices, area, boxStyle.cornerRadius, fill, boxStyle.MakeGradient(gradientEnd), pixelsPerUnit);
        if (boxStyle.borderWidth > 0.0f) {
            UIBoxMesh::AppendBorder(_boxVertices, _boxIndices, area, boxStyle.cornerRadius, boxStyle.borderWidth, boxStyle.borderColor, pixelsPerUnit);
        }
        UIBoxMesh::AppendHighlight(_boxVertices, _boxIndices, area, boxStyle.cornerRadius, boxStyle.highlight, pixelsPerUnit);
        if (!_boxVertices.empty()) {
            renderer.DrawGeometry(nullptr, _boxVertices, _boxIndices, 0.0f, 0.0f);
        }
    }

    void UIElement::DrawPart(IRenderer& renderer, const Rect& area, const Color& fill, UIPartShape shape) {
        if (shape == UIPartShape::Square) {
            renderer.DrawFilledRect(area.x, area.y, area.w, area.h, fill);
            return;
        }
        UIBoxStyle round;
        round.cornerRadius = UICorners::Uniform(std::min(area.w, area.h) * 0.5f);
        DrawBoxIn(renderer, area, round, fill, fill);
    }

    void UIElement::DrawSelf(IRenderer& renderer) {
        DrawBox(renderer, box, color, box.gradientColor);
    }

    UIContentFit UIElement::CheckContent(float /*width*/, float /*height*/, float /*scale*/) const {
        return {};
    }

}  // namespace BECore
