#pragma once

#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /**
     * @brief The text of a binding split into its parts: `source.path`
     *
     * `source` is the part before the first `.` (a name of the asset's `<sources>` or of `PanelDataSources`), `path` is a property path
     * of that source's object (`Reflection/PropertyPath.h`). `negated` is a leading `!` of a condition.
     */
    struct PanelBinding {
        String source;
        String path;
        bool negated = false;
    };

    /**
     * @brief Reads a binding text; the error is a short line that names what was expected
     *
     * Only the syntax is checked: whether the source exists is decided when the panel reads it. A `!` is an error here, a condition
     * (`visible`, `enableWhen`) is read by ParseCondition. Client text: nothing is interned, no input asserts.
     */
    [[nodiscard]] std::expected<PanelBinding, String> ParseBinding(StringView text);

    /// A binding of a bool field with an optional leading `!`
    [[nodiscard]] std::expected<PanelBinding, String> ParseCondition(StringView text);

    /// Whether @p name can be the name of a source: `[A-Za-z_][A-Za-z0-9_]*`, what ParseBinding takes before the first `.`
    [[nodiscard]] bool IsValidSourceName(StringView name);

}  // namespace BECore
