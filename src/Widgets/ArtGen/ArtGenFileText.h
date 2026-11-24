#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /// @p text without a leading UTF-8 byte order mark. A JsonTape refuses the mark, and a preset, a manifest or a provenance record saved by a
    /// Windows editor carries it, so every ArtGen reader of a file skips it before it tapes the text
    [[nodiscard]] inline StringView WithoutUtf8Bom(StringView text) {
        constexpr StringView kUtf8Bom = "\xEF\xBB\xBF";
        if (text.starts_with(kUtf8Bom)) {
            text.remove_prefix(kUtf8Bom.size());
        }
        return text;
    }

}  // namespace BECore
