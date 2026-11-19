#pragma once

#include <BECore/Types/Types.h>

namespace BECore::Tests {

    inline bool Contains(StringView text, StringView part) {
        return text.find(part) != StringView::npos;
    }

}  // namespace BECore::Tests
