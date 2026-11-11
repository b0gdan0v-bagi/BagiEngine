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

    void UIElement::Initialize() {
        // The second of a type is dropped, as `AddComponent` would never have made it
        for (size_t i = 0; i < components.size(); ++i) {
            const auto typeName = components[i]->GetTypeMeta().typeName;
            for (size_t j = 0; j < i; ++j) {
                if (components[j]->GetTypeMeta().typeName == typeName) {
                    LOG_WARNING("UIElement '{}': a second {} is ignored, an element holds one component of each type"_format(name.ToStringView(), typeName));
                    components.erase(components.begin() + static_cast<ptrdiff_t>(i));
                    --i;
                    break;
                }
            }
        }
    }

    bool UIElement::IsInteractive() const {
        if (interactive) {
            return true;
        }
        return std::any_of(components.begin(), components.end(), [](const auto& component) { return component->MakesInteractive(); });
    }

    UIStateLook::State UIElement::GetVisualState() const {
        // A press that moved off the element is not shown as pressed: letting go there does nothing
        return {hovered, pressed && hovered, IsDisabled(), selected};
    }

    bool UIElement::IsDisabled() const {
        return groupDisabled;
    }

    const eastl::string& UIElement::GetTooltipText() const {
        static const eastl::string none;
        return none;
    }

    UIStateLook::Look UIElement::GetStateLook() const {
        return UIStateLook::ResolveBox(box, color, {hoverColor, pressedColor, disabledColor, selectedColor}, GetVisualState(), GetStateAccent());
    }

    Color UIElement::GetStateAccent() const {
        return {255, 255, 255, 255};
    }

    bool UIElement::GivesStateLook() const {
        return std::any_of(components.begin(), components.end(), [](const auto& component) { return component->GivesStateLook(); });
    }

    void UIElement::DrawStateBox(IRenderer& renderer) {
        const UIStateLook::Look look = GetStateLook();
        DrawBox(renderer, look.box, look.fill, look.gradientEnd);
        if (!selected) {
            return;
        }
        const Rect bar = UIStateLook::SelectedBar(rect, box);
        if (!bar.IsEmpty()) {
            const Color accent = GetStateAccent();
            renderer.DrawFilledRect(bar.x, bar.y, bar.w, bar.h, IsDisabled() ? WithAlpha(accent, kDisabledAlpha) : accent);
        }
    }

    void UIElement::DrawComponents(IRenderer& renderer) {
        // By index and with a copy of the pointer: a component that changes the list while it draws does not invalidate the loop
        for (size_t i = 0; i < components.size(); ++i) {
            const IntrusivePtrNonAtomic<UIComponent> component = components[i];
            component->DrawSelf(renderer, *this);
        }
    }

    void UIElement::OnClick() {
        for (size_t i = 0; i < components.size(); ++i) {
            const IntrusivePtrNonAtomic<UIComponent> component = components[i];
            component->OnClick(*this);
        }
    }

    void UIElement::OnPress(float x, float y) {
        for (size_t i = 0; i < components.size(); ++i) {
            const IntrusivePtrNonAtomic<UIComponent> component = components[i];
            component->OnPress(*this, x, y);
        }
    }

    void UIElement::OnDrag(float x, float y) {
        for (size_t i = 0; i < components.size(); ++i) {
            const IntrusivePtrNonAtomic<UIComponent> component = components[i];
            component->OnDrag(*this, x, y);
        }
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
        if (GivesStateLook()) {
            DrawStateBox(renderer);
            return;
        }
        DrawBox(renderer, box, color, box.gradientColor);
    }

    UIContentFit UIElement::CheckContent(float width, float height, float scale) const {
        bool hasContent = false;
        bool defect = false;  // a part that does not fit and cannot shrink
        bool shrunk = false;  // a part that does not fit but cuts or shrinks its content
        for (const auto& component : components) {
            const UIContentFit fit = component->CheckContent(*this, width, height, scale);
            if (!fit.hasContent) {
                continue;
            }
            hasContent = true;
            if (!fit.fits) {
                (fit.canShrink ? shrunk : defect) = true;
            }
        }
        return {hasContent, !defect && !shrunk, shrunk && !defect};
    }

}  // namespace BECore
