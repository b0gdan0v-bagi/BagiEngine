#pragma once

#include <BECore/Types/Types.h>
#include <Stress/SpriteField.h>
#include <Stress/StressSettings.h>
#include <cstdint>

namespace BECore::Stress {

    /// The text lines of a generated document, each at its own font size: a document makes one glyph texture per size, so the count of
    /// documents times this decides how many textures the renderer holds
    inline constexpr uint32_t kRmlUiSizes = 6;

    /// The `font-size` of line `k` of a document, in `dp`; all distinct
    inline constexpr uint32_t kRmlUiFontSizes[kRmlUiSizes] = {10, 12, 14, 17, 20, 24};

    /**
     * @brief The RML text of generated document @p index of @p count: one block in the cell of the grid of @p count documents in @p view, with
     * `kRmlUiSizes` lines of text.
     *
     * A function of its arguments alone. The texts are the plain labels of `TextOf` (English for an even index, Russian for an odd one, swapped
     * for `Ru`), never a `{#key}` or a `{variable}`. `body` says `pointer-events: none`, so a wheel over a document reaches the panel below it.
     */
    String MakeDocumentMarkup(uint32_t index, uint32_t count, StressLanguage language, const FieldView& view);

}  // namespace BECore::Stress
