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
        if (rect.IsEmpty()) {
            return;
        }
        if (!boxStyle.HasEffect()) {
            if (fill.a > 0) {
                renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, fill);
            }
            return;
        }

        const float pixelsPerUnit = renderer.GetViewTransform().scale;
        _boxVertices.clear();
        _boxIndices.clear();
        // Each appender draws nothing for a colour with no alpha, so the order is all that is decided here
        UIBoxMesh::AppendShadow(_boxVertices, _boxIndices, rect, boxStyle.cornerRadius, boxStyle.shadow, pixelsPerUnit);
        UIBoxMesh::AppendShadow(_boxVertices, _boxIndices, rect, boxStyle.cornerRadius, boxStyle.glow, pixelsPerUnit);
        UIBoxMesh::AppendFill(_boxVertices, _boxIndices, rect, boxStyle.cornerRadius, fill, boxStyle.MakeGradient(gradientEnd), pixelsPerUnit);
        if (boxStyle.borderWidth > 0.0f) {
            UIBoxMesh::AppendBorder(_boxVertices, _boxIndices, rect, boxStyle.cornerRadius, boxStyle.borderWidth, boxStyle.borderColor, pixelsPerUnit);
        }
        UIBoxMesh::AppendHighlight(_boxVertices, _boxIndices, rect, boxStyle.cornerRadius, boxStyle.highlight, pixelsPerUnit);
        if (!_boxVertices.empty()) {
            renderer.DrawGeometry(nullptr, _boxVertices, _boxIndices, 0.0f, 0.0f);
        }
    }

    void UIElement::DrawSelf(IRenderer& renderer) {
        DrawBox(renderer, box, color, box.gradientColor);
    }

    UIContentFit UIElement::CheckContent(float /*width*/, float /*height*/, float /*scale*/) const {
        return {};
    }

}  // namespace BECore
