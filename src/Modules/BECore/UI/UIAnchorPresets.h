#pragma once

#include <BECore/UI/UIRectEdit.h>

namespace BECore {

    /// One cell of the anchor grid
    struct UIAnchorPreset {
        uint8_t row = 0;     ///< 0 top, 1 middle, 2 bottom, 3 stretch
        uint8_t column = 0;  ///< 0 left, 1 center, 2 right, 3 stretch
        Vec2 anchorMin;
        Vec2 anchorMax;
        Vec2 pivot;

        [[nodiscard]] constexpr bool operator==(const UIAnchorPreset&) const = default;
    };

    /// What a click on a preset also does (Unity's Shift and Alt)
    struct UIAnchorApplyMode {
        bool setPivot = false;     ///< take the pivot of the preset too
        bool setPosition = false;  ///< also put the element on the anchors: a point axis at zero offset, a stretched axis with no margin
    };

    /**
     * @brief The anchor grid of Unity: three by three points, then a stretch row and a stretch column on each axis, and stretch on both.
     *
     * Pure like UILayout. The nine points and the stretch of both axes are the shorthands of `UIRectSpec::anchor`; the other six are edge anchors
     * only.
     */
    class UIAnchorPresets {
    public:
        static constexpr size_t kRows = 4;
        static constexpr size_t kColumns = 4;
        static constexpr uint8_t kStretch = 3;  ///< the row or column index that stretches

        /// All sixteen, row by row: the order the grid is drawn in
        [[nodiscard]] static Span<const UIAnchorPreset> All();

        /// @return the preset at @p row and @p column (each 0..3)
        [[nodiscard]] static const UIAnchorPreset& At(size_t row, size_t column);

        /// The English name of a preset ("Top Left", "Stretch Bottom", "Stretch All"), for a tooltip
        [[nodiscard]] static const char* Name(const UIAnchorPreset& preset);

        /// @return the preset whose min, max and pivot are the effective anchors of @p spec (so a shorthand matches too), or nullptr for custom anchors
        [[nodiscard]] static const UIAnchorPreset* Match(const UIRectSpec& spec);

        /**
         * @brief @p spec with the anchors of @p preset, laid out in @p parent where it was before.
         *
         * Without `setPivot` the pivot stays the effective one. The spec is written as the shorthand (`anchor`, the three optional fields cleared) when
         * the anchors are one of the ten, and as the explicit fields otherwise. The rect is kept through UIRectEdit::SpecForRect, so only offsets, sizes
         * and margins change; with `setPosition` the element goes to the anchors instead (the size stays).
         * @param parent the box of the element: its parent, or the region it names
         * @return the spec; `exact` is false when the limits or the aspect ratio keep the rect from being kept
         */
        [[nodiscard]] static UIRectEditResult Apply(const Rect& parent, const UIRectSpec& spec, const UIAnchorPreset& preset, UIAnchorApplyMode mode = {});

        /// Writes the shorthand `anchor` and clears the optional fields when the effective anchors of @p spec are one of the ten; nothing else changes
        static void Simplify(UIRectSpec& spec);
    };

}  // namespace BECore
