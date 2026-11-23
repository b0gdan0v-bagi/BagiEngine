#include "UIDropdown.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIVars.h>
#include <Events/InputEvents.h>
#include <Generated/UIDropdown.gen.hpp>

namespace BECore {

    UIDropdown::UIDropdown() {
        interactive = true;
    }

    UIDropdown::HeaderParts UIDropdown::GetHeaderParts(const Rect& header) {
        HeaderParts parts;
        const float arrowWidth = std::min(kArrowWidth, std::max(0.0f, header.w - 2.0f * kArrowMargin));
        const float arrowHeight = arrowWidth * kArrowAspect;
        parts.arrow = {header.x + header.w - kArrowMargin - arrowWidth, header.y + (header.h - arrowHeight) * 0.5f, arrowWidth, arrowHeight};
        const float textLeft = header.x + kTextInset;
        const float textRight = (arrowWidth > 0.0f ? parts.arrow.x : header.x + header.w) - kArrowGap;
        parts.text = {textLeft, header.y, std::max(0.0f, textRight - textLeft), header.h};
        return parts;
    }

    Array<Rect, UIDropdown::kArrowRows> UIDropdown::GetArrowStrips(const Rect& box, bool pointsUp) {
        Array<Rect, kArrowRows> strips;
        const float rowHeight = box.h / static_cast<float>(kArrowRows);
        for (int row = 0; row < kArrowRows; ++row) {
            // Row 0 is the top of the box; a down arrow is widest there, an up arrow narrowest
            const int fromTip = pointsUp ? row : kArrowRows - 1 - row;
            const float width = box.w * static_cast<float>(fromTip + 1) / static_cast<float>(kArrowRows);
            strips[static_cast<size_t>(row)] = {box.x + (box.w - width) * 0.5f, box.y + rowHeight * static_cast<float>(row), width, rowHeight};
        }
        return strips;
    }

    UIDropdown::PopupPlacement UIDropdown::PlacePopup(const Rect& header, Span<const OptionSize> options, const Rect& bounds, Span<Rect> optionRects) {
        ASSERT(optionRects.size() == options.size(), "one rect slot per option");
        PopupPlacement placement;
        float width = header.w;
        float height = 0.0f;
        for (const OptionSize& option : options) {
            width = std::max(width, option.width);
            height += option.height;
        }
        width = std::min(width, bounds.w);

        // Left edges aligned with the header; moved left when the column would leave the bounds on the right
        const float x = std::max(bounds.x, std::min(header.x, bounds.x + bounds.w - width));
        const float below = header.y + header.h;
        const float above = header.y - height;
        float y = below;
        if (below + height > bounds.y + bounds.h) {
            if (above >= bounds.y) {
                y = above;
                placement.above = true;
            } else {
                y = std::max(bounds.y, bounds.y + bounds.h - height);
            }
        }

        placement.list = {x, y, width, height};
        float top = y;
        for (size_t i = 0; i < options.size(); ++i) {
            optionRects[i] = {x, top, width, options[i].height};
            top += options[i].height;
        }
        return placement;
    }

    UIDropdown::PopupGeometry UIDropdown::GetPopupGeometry(const Rect& header, const Vector<OptionSize>& options, const Rect& bounds) {
        PopupGeometry geometry;
        geometry.options.resize(options.size());
        const PopupPlacement placement = PlacePopup(header, Span<const OptionSize>(options.data(), options.size()), bounds, Span<Rect>(geometry.options.data(), geometry.options.size()));
        geometry.list = placement.list;
        geometry.above = placement.above;
        return geometry;
    }

    void UIDropdown::Open() {
        _open = !disabled;
    }

    void UIDropdown::Close() {
        _open = false;
    }

    void UIDropdown::Toggle() {
        if (_open) {
            Close();
        } else {
            Open();
        }
    }

    StringView UIDropdown::GetSelectedValue() const {
        if (bind.Empty()) {
            return {};
        }
        return CoreManager::GetUIVars().Get(bind);
    }

    UIElement* UIDropdown::FindChosenOption() const {
        const StringView chosen = GetSelectedValue();
        if (chosen.empty()) {
            return nullptr;
        }
        for (const auto& child : children) {
            if (UIChoice::IsOption(*child) && UIChoice::GetValue(*child).ToStringView() == chosen) {
                return child.Get();
            }
        }
        return nullptr;
    }

    void UIDropdown::SyncChildren() {
        if (disabled) {
            _open = false;  // a list never stays open behind a dropdown that got disabled
        }
        const UIElement* chosen = FindChosenOption();
        for (const auto& child : children) {
            if (!UIChoice::IsOption(*child)) {
                continue;
            }
            child->selected = child.Get() == chosen;
            child->groupDisabled = disabled;
        }
    }

    Rect UIDropdown::GetComponentArea() const {
        return GetHeaderParts(rect).text;
    }

    const String* UIDropdown::GetTextOverride() const {
        const UIElement* chosen = FindChosenOption();
        return chosen ? &UIChoice::GetText(*chosen) : nullptr;
    }

    Color UIDropdown::GetCurrentFill() const {
        return GetCurrentBox().fill;
    }

    UIStateLook::Look UIDropdown::GetCurrentBox() const {
        UIStateLook::Look look{box, color, box.gradientColor};
        if (disabled) {
            look.fill = WithAlpha(look.fill, kDisabledAlpha);
            look.gradientEnd = WithAlpha(look.gradientEnd, kDisabledAlpha);
            if (box.HasEffect()) {
                look.box.borderColor = WithAlpha(look.box.borderColor, kDisabledAlpha);
                look.box.highlight = WithAlpha(look.box.highlight, kDisabledAlpha);
                look.box.shadow = {};
                look.box.glow = {};
            }
        } else if (pressed && hovered) {
            look.fill = ShiftColor(look.fill, -kPressDarken);
            look.gradientEnd = ShiftColor(look.gradientEnd, -kPressDarken);
        } else if (hovered) {
            look.fill = ShiftColor(look.fill, kHoverBrighten);
            look.gradientEnd = ShiftColor(look.gradientEnd, kHoverBrighten);
        }
        if (look.box.gradientDirection != UIGradientDirection::None) {
            look.box.gradientColor = look.gradientEnd;  // the look says the far end twice, as a button's does
        }
        return look;
    }

    Color UIDropdown::GetCurrentArrow() const {
        const UITextComponent* header = GetComponent<UITextComponent>();
        const Color base = arrowColor.a > 0 ? Color(arrowColor) : (header ? Color(header->textColor) : Color(255, 255, 255, 255));
        return disabled ? WithAlpha(base, kDisabledAlpha) : base;
    }

    void UIDropdown::ApplyTheme(UIThemes& themes) {
        UIElement::ApplyTheme(themes);
        themes.Resolve(arrowColor);
    }

    void UIDropdown::DrawSelf(IRenderer& renderer) {
        SyncChildren();
        if (rect.IsEmpty()) {
            return;
        }
        // The box, then the arrow; the header text is the component, which the canvas draws right after this (`DrawComponents`), left of the arrow
        const UIStateLook::Look target = GetCurrentBox();
        const UIStateLook::Look& look = ShownLook(target);
        DrawBox(renderer, look.box, look.fill, look.gradientEnd);

        const HeaderParts parts = GetHeaderParts(rect);

        if (!parts.arrow.IsEmpty()) {
            const Color arrow = GetCurrentArrow();
            for (const Rect& strip : GetArrowStrips(parts.arrow, IsPopupOpen())) {
                renderer.DrawFilledRect(strip.x, strip.y, strip.w, strip.h, arrow);
            }
        }
    }

    void UIDropdown::OnClick() {
        if (!disabled) {
            Toggle();
        }
    }

    void UIDropdown::LayoutPopup(const Rect& contentBounds) {
        // Runs per layout pass: the working lists are scratch, so a warm arena makes no heap allocation
        const ScratchScope scratch;
        // An option that is not shown takes no room: the layout gives it an empty rect afterwards
        ArenaVector<UIElement*> shown;
        ArenaVector<OptionSize> sizes;
        shown.reserve(children.size());
        sizes.reserve(children.size());
        for (const auto& child : children) {
            UIElement* option = child.Get();
            if (!UIChoice::IsOption(*option) || !option->active.visible) {
                continue;
            }
            const UIRectSpec& spec = option->active.rect;
            const float authoredHeight = spec.height.unit == UIUnit::Pixels ? spec.height.value : 0.0f;
            const float authoredWidth = spec.width.unit == UIUnit::Pixels ? spec.width.value : 0.0f;
            OptionSize size;
            size.width = authoredWidth;
            size.height = authoredHeight > 0.0f ? authoredHeight : (optionHeight > 0.0f ? optionHeight : rect.h);
            shown.push_back(option);
            sizes.push_back(size);
        }

        ArenaVector<Rect> rects(shown.size());
        PlacePopup(rect, Span<const OptionSize>(sizes.data(), sizes.size()), contentBounds, Span<Rect>(rects.data(), rects.size()));
        for (size_t i = 0; i < shown.size(); ++i) {
            shown[i]->rect = rects[i];
        }
    }

    bool UIDropdown::EmitsOnChoice(const UIElement& option) const {
        return !disabled && !action.Empty() && !option.IsDisabled();
    }

    void UIDropdown::OnPopupChoice(UIElement& child) {
        if (!UIChoice::IsOption(child)) {
            return;
        }
        if (child.IsDisabled()) {
            return;  // takes the click and does nothing; the list stays open
        }
        const bool emit = EmitsOnChoice(child);
        const PoolString value = UIChoice::GetValue(child);
        Close();
        if (emit) {
            InputEvents::ActionEvent::Emit(action, value);
        }
    }

    UIContentFit UIDropdown::CheckContent(float width, float height, float scale) const {
        const HeaderParts parts = GetHeaderParts({0.0f, 0.0f, width, height});
        const UITextComponent* header = GetComponent<UITextComponent>();
        if (parts.text.w <= 0.0f && header && !header->GetDisplayText(*this).empty() && width > 0.0f && height > 0.0f) {
            // The arrow takes the whole width: the text has no room at all, and nothing can shrink to nothing
            UIContentFit none;
            none.hasContent = true;
            none.fits = false;
            return none;
        }
        return UIElement::CheckContent(parts.text.w, parts.text.h, scale);
    }

}  // namespace BECore
