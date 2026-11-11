#include "UIStateLook.h"

namespace BECore {

    namespace {

        unsigned char ShiftChannel(unsigned char channel, int delta) {
            return static_cast<unsigned char>(std::clamp(static_cast<int>(channel) + delta, 0, 255));
        }

    }  // namespace

    UIStateLook::Look UIStateLook::ResolveBox(const UIBoxStyle& box, const Color& color, const Colors& stateColors, const State& state, const Color& accent) {
        Look look{box, color, box.gradientColor};
        const bool styled = box.HasEffect();

        // The explicit colour of a state replaces the start, the far end keeps its offset from it
        const auto replaceStart = [&look](const Color& start) {
            look.gradientEnd = {ShiftChannel(look.gradientEnd.r, start.r - look.fill.r), ShiftChannel(look.gradientEnd.g, start.g - look.fill.g),
                                ShiftChannel(look.gradientEnd.b, start.b - look.fill.b), ShiftChannel(look.gradientEnd.a, start.a - look.fill.a)};
            look.fill = start;
        };
        const auto shiftBoth = [&look](int delta) {
            look.fill = ShiftColor(look.fill, delta);
            look.gradientEnd = ShiftColor(look.gradientEnd, delta);
        };

        // The look says the far end twice (the field and the member); a box without a gradient has no end to keep
        const auto keepEnd = [&look]() {
            if (look.box.gradientDirection != UIGradientDirection::None) {
                look.box.gradientColor = look.gradientEnd;
            }
        };

        if (state.selected && styled) {
            look.box.borderColor = accent;
            look.box.borderWidth = std::max(look.box.borderWidth, kSelectedBorderMin);
            if (look.box.glow.color.a == 0) {
                look.box.glow = {0.0f, 0.0f, kSelectedGlowBlur, 0.0f, WithAlpha(accent, kSelectedGlowAlpha)};
            }
        }

        if (state.disabled) {
            if (stateColors.disabled.a > 0) {
                replaceStart(stateColors.disabled);
            } else {
                look.fill = WithAlpha(look.fill, kDisabledAlpha);
                look.gradientEnd = WithAlpha(look.gradientEnd, kDisabledAlpha);
            }
            if (styled) {
                look.box.borderColor = WithAlpha(look.box.borderColor, kDisabledAlpha);
                look.box.highlight = WithAlpha(look.box.highlight, kDisabledAlpha);
                look.box.shadow = {};
                look.box.glow = {};
            }
            keepEnd();
            return look;
        }

        if (state.selected) {
            if (stateColors.selected.a > 0) {
                replaceStart(stateColors.selected);
            } else {
                shiftBoth(kSelectedBrighten);
            }
        }
        if (state.pressed) {
            if (stateColors.pressed.a > 0) {
                replaceStart(stateColors.pressed);
            } else {
                shiftBoth(-kPressDarken);
            }
            if (styled) {
                look.box.borderColor = ShiftColor(look.box.borderColor, -kPressDarken);
                look.box.shadow.offsetY *= kPressShadowOffsetScale;
                look.box.shadow.blur *= kPressShadowBlurScale;
                look.box.highlight = {};
            }
        } else if (state.hovered) {
            if (stateColors.hover.a > 0) {
                replaceStart(stateColors.hover);
            } else {
                shiftBoth(kHoverBrighten);
            }
            if (styled) {
                look.box.borderColor = ShiftColor(look.box.borderColor, kHoverBrighten);
            }
        }
        keepEnd();
        return look;
    }

    Color UIStateLook::ResolveTint(const Color& base, const Tints& tints, const State& state) {
        if (state.disabled) {
            return tints.disabled.a > 0 ? tints.disabled : WithAlpha(base, kDisabledAlpha);
        }
        if (state.pressed) {
            return tints.pressed.a > 0 ? tints.pressed : ShiftColor(base, -kPressDarken);
        }
        if (state.hovered) {
            return tints.hover.a > 0 ? tints.hover : ShiftColor(base, kHoverBrighten);
        }
        return base;
    }

    Rect UIStateLook::SelectedBar(const Rect& rect, const UIBoxStyle& box) {
        if (rect.IsEmpty()) {
            return {};
        }
        const float height = std::min(kSelectedBarHeight, rect.h);
        // The rounded bottom corners are left to the shape: the bar spans the straight part of the bottom edge
        const UICorners radii = box.cornerRadius.Clamped(rect.w, rect.h);
        const float left = radii.bottomLeft;
        const float width = rect.w - left - radii.bottomRight;
        if (width <= 0.0f) {
            return {};
        }
        return {rect.x + left, rect.y + rect.h - height, width, height};
    }

}  // namespace BECore
