#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// Standard Base64 (RFC 4648, section 4) with padding
    [[nodiscard]] String Base64Encode(Span<const uint8_t> data);

}  // namespace BECore
