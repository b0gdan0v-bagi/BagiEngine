#include "RmlUiField.h"

#include <Stress/UiField.h>

namespace BECore::Stress {

    namespace {

        /// How far from its first candidate a plain label can lie: the keys and the variables are two of every eight indices
        constexpr uint32_t kMaxLabelSearch = 16;

        /// The text of the first plain label (not a key, not a variable) of the wanted language at or after @p first
        StringView PlainTextFrom(uint32_t first, bool english) {
            const UiLabelKind wanted = english ? UiLabelKind::English : UiLabelKind::Russian;
            for (uint32_t offset = 0; offset < kMaxLabelSearch; ++offset) {
                if (KindOf(first + offset) == wanted) {
                    return TextOf(first + offset);
                }
            }
            return TextOf(first);
        }

    }  // namespace

    String MakeDocumentMarkup(uint32_t index, uint32_t count, StressLanguage language, const FieldView& view) {
        const UiCell cell = CellOf(index, MakeGrid(count, view));
        const bool english = ((index % 2 == 0) == (language == StressLanguage::En));

        String lines;
        for (uint32_t line = 0; line < kRmlUiSizes; ++line) {
            const StringView text = PlainTextFrom(index * kRmlUiSizes + line, english);
            lines += Format("<div style=\"font-size: {}dp;\">{}</div>", kRmlUiFontSizes[line], text);
        }
        return Format("<rml><head><style>"
                      "body {{ width: 100%; height: 100%; font-family: \"PT Sans\"; color: white; pointer-events: none; }} "
                      "div {{ display: block; white-space: nowrap; }}"
                      "</style></head><body>"
                      "<div style=\"position: absolute; left: {}dp; top: {}dp; width: {}dp; height: {}dp; overflow: hidden;\">{}</div>"
                      "</body></rml>",
                      cell.x, cell.y, cell.width, cell.height, lines);
    }

}  // namespace BECore::Stress
