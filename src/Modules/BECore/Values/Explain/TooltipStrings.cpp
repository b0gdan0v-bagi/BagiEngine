#include "TooltipStrings.h"

namespace BECore {

    TooltipWords MakeTooltipWords(const UIStrings& strings) {
        TooltipWords words;
        if (const Optional<StringView> pattern = strings.Find(kTooltipPerKey)) {
            words.perPattern = String(*pattern);
        }
        words.label = [&strings](StringView path) -> Optional<String> {
            const Optional<StringView> label = strings.Find(Format("{}{}", kTooltipLabelPrefix, path));
            if (!label.has_value()) {
                return std::nullopt;
            }
            return String(*label);
        };
        return words;
    }

}  // namespace BECore
