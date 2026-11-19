#pragma once

#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElement.h>

namespace BECore::Tests {

    inline IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height) {
        auto box = parent.AddChild(PoolString::Intern(name));
        box->style.rect.x = UILength::Px(x);
        box->style.rect.y = UILength::Px(y);
        box->style.rect.width = UILength::Px(width);
        box->style.rect.height = UILength::Px(height);
        return box;
    }

    /// A game HUD: a panel top left with a score and a Reset button. The button is 48 units high and the canvas never scales below 0.7, so on
    /// a near-square window (where ScaleWithScreen goes down to 0.5) it is still the 32 window units that the validator asks for.
    inline void BuildHud(UICanvasLayout& canvas) {
        canvas.params.useSafeArea = true;
        canvas.params.minScale = 0.7f;
        auto hud = AddBox(*canvas.root, "Hud", 16.0f, 16.0f, 220.0f, 96.0f);
        hud->blocksInput = true;
        hud->color = Color{0, 0, 0, 160};
        AddBox(*hud, "Score", 12.0f, 10.0f, 120.0f, 28.0f);
        auto reset = AddBox(*hud, "Reset", -12.0f, -10.0f, 96.0f, 48.0f);
        reset->style.rect.anchor = UIAnchor::BottomRight;
        reset->interactive = true;
    }

}  // namespace BECore::Tests
