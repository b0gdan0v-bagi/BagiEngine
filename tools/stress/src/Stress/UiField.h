#pragma once

#include <Stress/SpriteField.h>

namespace BECore::Stress {

    /// A cell of the grid is about this much wider than it is high: the labels are sentences, not squares
    inline constexpr float kUiCellAspect = 4.0f;
    /// The font size of a label by its row: `kUiMinFontSize` + `kUiFontStep` * (row % `kUiFontSizes`), so 14..28
    inline constexpr float kUiMinFontSize = 14.0f;
    inline constexpr float kUiFontStep = 2.0f;
    inline constexpr uint32_t kUiFontSizes = 8;
    /// The texts of the labels come from pools of this many sentences per language, so the glyph atlas does not grow without end
    inline constexpr uint32_t kUiPoolSize = 16;
    /// Every this many labels (from the first) is a `{#stress.label.<k>}` key of `config/UIStrings.xml`: the labels a language switch rewords
    inline constexpr uint32_t kUiKeyEvery = 8;

    /// The grid the labels are placed on: `columns` x `rows` cells that fill the canvas
    struct UiGrid {
        uint32_t columns = 1;
        uint32_t rows = 1;
        float cellWidth = 0.0f;
        float cellHeight = 0.0f;
    };

    /// One label's place, in the units of the canvas (the view of the scene)
    struct UiCell {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float fontSize = 0.0f;
    };

    /// The grid for @p count labels in @p view: the columns follow the aspect of the view and of a cell (`kUiCellAspect`), so the cells stay wide whatever the count; the whole grid is inside the view
    UiGrid MakeGrid(uint32_t count, const FieldView& view);

    /// Label @p index in row-major order; its font size follows its row
    UiCell CellOf(uint32_t index, const UiGrid& grid);

    /// What a label shows: a literal sentence, or a text with a `{#key}` or a `{variable}` in it
    CORE_ENUM(UiLabelKind, uint8_t, English, Russian, Key, Variable)

    /// The kind of label @p index: every `kUiKeyEvery`th is a `Key`, the other even indices are English and the odd ones Russian, and the fifth and the eighth of every eight hold a `Variable`
    UiLabelKind KindOf(uint32_t index);

    /// The text of label @p index, a view of a static table: no allocation, the same on every machine. A `Key` label is `{#stress.label.<k>}`, a `Variable` one names `{count}` or `{last}`
    /// (the variables the scene sets: the count and the index of the last label)
    StringView TextOf(uint32_t index);

    /// The key of `config/UIStrings.xml` a `Key` label refers to (without the braces), `stress.label.<k>` with k below `kUiPoolSize`
    StringView KeyOf(uint32_t index);

}  // namespace BECore::Stress
