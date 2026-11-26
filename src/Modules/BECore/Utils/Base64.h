#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// Standard Base64 (RFC 4648, section 4) with padding
    [[nodiscard]] String Base64Encode(Span<const uint8_t> data);

    /**
     * @brief Decodes what Base64Encode() writes into @p data, whose capacity is reused.
     *
     * Strict, since the text may be a peer's: false, with @p data empty, for a length that is no multiple of 4, a character outside the alphabet
     * (white space too), padding anywhere but at the end, or bits under the padding that are not zero.
     */
    [[nodiscard]] bool Base64Decode(StringView text, Vector<uint8_t>& data);

}  // namespace BECore
