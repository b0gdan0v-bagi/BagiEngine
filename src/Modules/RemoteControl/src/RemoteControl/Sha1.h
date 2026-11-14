#pragma once

namespace BECore {

    using Sha1Digest = std::array<uint8_t, 20>;

    /// SHA-1 (FIPS 180-4) of @p data. Only the WebSocket handshake uses it (RFC 6455, section 4.2.2); it is not a security primitive here.
    [[nodiscard]] Sha1Digest Sha1(Span<const uint8_t> data);

}  // namespace BECore
