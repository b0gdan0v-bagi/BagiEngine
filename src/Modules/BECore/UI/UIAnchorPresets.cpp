#include "UIAnchorPresets.h"

#include <BECore/UI/UILayout.h>

namespace BECore {

    namespace {

        constexpr float kAnchorTolerance = 1e-5f;  // a typed 50% may not be exactly 0.5

        /// The anchors of one axis for each index of the grid: left / top, center / middle, right / bottom, stretch
        constexpr std::array<float, UIAnchorPresets::kRows> kMin = {0.0f, 0.5f, 1.0f, 0.0f};
        constexpr std::array<float, UIAnchorPresets::kRows> kMax = {0.0f, 0.5f, 1.0f, 1.0f};
        constexpr std::array<float, UIAnchorPresets::kRows> kPivot = {0.0f, 0.5f, 1.0f, 0.5f};

        constexpr UIAnchorPreset MakePreset(size_t row, size_t column) {
            UIAnchorPreset preset;
            preset.row = static_cast<uint8_t>(row);
            preset.column = static_cast<uint8_t>(column);
            preset.anchorMin = {kMin[column], kMin[row]};
            preset.anchorMax = {kMax[column], kMax[row]};
            preset.pivot = {kPivot[column], kPivot[row]};
            return preset;
        }

        constexpr std::array<UIAnchorPreset, UIAnchorPresets::kRows * UIAnchorPresets::kColumns> MakeGrid() {
            std::array<UIAnchorPreset, UIAnchorPresets::kRows * UIAnchorPresets::kColumns> grid;
            for (size_t row = 0; row < UIAnchorPresets::kRows; ++row) {
                for (size_t column = 0; column < UIAnchorPresets::kColumns; ++column) {
                    grid[row * UIAnchorPresets::kColumns + column] = MakePreset(row, column);
                }
            }
            return grid;
        }

        constexpr auto kGrid = MakeGrid();

        constexpr std::array<const char*, UIAnchorPresets::kRows * UIAnchorPresets::kColumns> kNames = {"Top Left",     "Top Center",     "Top Right",     "Stretch Top",     //
                                                                                                        "Middle Left",  "Middle Center",  "Middle Right",  "Stretch Middle",  //
                                                                                                        "Bottom Left",  "Bottom Center",  "Bottom Right",  "Stretch Bottom",  //
                                                                                                        "Stretch Left", "Stretch Center", "Stretch Right", "Stretch All"};

        constexpr UIAnchor kShorthands[] = {UIAnchor::TopLeft, UIAnchor::Top,        UIAnchor::TopRight, UIAnchor::Left,        UIAnchor::Center,
                                            UIAnchor::Right,   UIAnchor::BottomLeft, UIAnchor::Bottom,   UIAnchor::BottomRight, UIAnchor::Stretch};

        bool Near(float a, float b) {
            return std::abs(a - b) <= kAnchorTolerance;
        }

        bool Near(const Vec2& a, const Vec2& b) {
            return Near(a.x, b.x) && Near(a.y, b.y);
        }

        bool Is(const UIEffectiveAnchors& anchors, const Vec2& min, const Vec2& max, const Vec2& pivot) {
            return Near(anchors.min, min) && Near(anchors.max, max) && Near(anchors.pivot, pivot);
        }

    }  // namespace

    eastl::span<const UIAnchorPreset> UIAnchorPresets::All() {
        return {kGrid.data(), kGrid.size()};
    }

    const UIAnchorPreset& UIAnchorPresets::At(size_t row, size_t column) {
        ASSERT(row < kRows && column < kColumns);
        return kGrid[std::min(row, kRows - 1) * kColumns + std::min(column, kColumns - 1)];
    }

    const char* UIAnchorPresets::Name(const UIAnchorPreset& preset) {
        return kNames[std::min<size_t>(preset.row, kRows - 1) * kColumns + std::min<size_t>(preset.column, kColumns - 1)];
    }

    const UIAnchorPreset* UIAnchorPresets::Match(const UIRectSpec& spec) {
        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);
        for (const UIAnchorPreset& preset : kGrid) {
            if (Is(anchors, preset.anchorMin, preset.anchorMax, preset.pivot)) {
                return &preset;
            }
        }
        return nullptr;
    }

    void UIAnchorPresets::Simplify(UIRectSpec& spec) {
        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);
        for (const UIAnchor shorthand : kShorthands) {
            UIRectSpec probe;
            probe.anchor = shorthand;
            const UIEffectiveAnchors named = UILayout::EffectiveAnchors(probe);
            if (Is(anchors, named.min, named.max, named.pivot)) {
                spec.anchor = shorthand;
                spec.anchorMin.reset();
                spec.anchorMax.reset();
                spec.pivot.reset();
                return;
            }
        }
    }

    UIRectEditResult UIAnchorPresets::Apply(const Rect& parent, const UIRectSpec& spec, const UIAnchorPreset& preset, UIAnchorApplyMode mode) {
        const Rect current = UILayout::ResolveRect(parent, spec);
        const UIEffectiveAnchors before = UILayout::EffectiveAnchors(spec);

        UIRectSpec next = spec;
        next.anchorMin = preset.anchorMin;
        next.anchorMax = preset.anchorMax;
        next.pivot = mode.setPivot ? preset.pivot : before.pivot;
        Simplify(next);

        Rect wanted = current;
        if (mode.setPosition) {
            // The element where the anchors put it: no offset, no margin, the size it has now (a stretched axis fills its span)
            UIRectSpec onAnchors = next;
            onAnchors.x = UILength::Px(0.0f);
            onAnchors.y = UILength::Px(0.0f);
            onAnchors.margin = {};
            onAnchors.width = UILength::Px(current.w);
            onAnchors.height = UILength::Px(current.h);
            wanted = UILayout::ResolveRect(parent, onAnchors);
        }
        return UIRectEdit::SpecForRect(parent, next, wanted);
    }

}  // namespace BECore
