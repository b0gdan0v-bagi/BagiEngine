#pragma once

namespace BECore {

    /// Standard Base64 (RFC 4648, section 4) with padding: the screenshot PNG and the WebSocket handshake use it
    [[nodiscard]] String Base64Encode(eastl::span<const uint8_t> data);

}  // namespace BECore
