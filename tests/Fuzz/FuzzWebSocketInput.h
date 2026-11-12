#pragma once

#include <RemoteControl/WebSocketFrame.h>
#include <RemoteControl/WebSocketHandshake.h>

namespace BECore::Fuzz::WebSocketInput {

    // =============================================================================
    // FuzzWebSocketFrame: the first byte is [pattern: 4 bits | limit: 3 bits | peer: 1 bit], the rest is the byte stream
    // =============================================================================

    /// The message limits a frame input can pick (the index is the three bits above the peer bit, modulo the count)
    inline constexpr size_t kMessageLimits[] = {0, 1, 125, 126, 1024, 65536};
    inline constexpr size_t kMessageLimitCount = std::size(kMessageLimits);

    inline constexpr uint8_t kPeerBit = 0x01;
    inline constexpr uint8_t kLimitShift = 1;
    inline constexpr uint8_t kLimitMask = 0x07;
    inline constexpr uint8_t kPatternShift = 4;

    /// The first byte of a frame input. Pattern 0 feeds the stream in one piece, the others in chunks of a pattern-dependent size
    constexpr uint8_t PackFrameInput(WebSocketPeer peer, size_t limitIndex, uint8_t pattern) {
        return static_cast<uint8_t>((peer == WebSocketPeer::Server ? kPeerBit : 0) | ((limitIndex & kLimitMask) << kLimitShift) | (pattern << kPatternShift));
    }

    // =============================================================================
    // FuzzHandshake: the first byte picks the policy (modulo the count), the rest is the text of a request head, a response head or a URL
    // =============================================================================

    CORE_ENUM(HandshakePolicyKind, uint8_t, Loopback, PublicWithOrigins, PublicAnyOrigin, PublicNoMissingOrigin, PublicWithHealthPath)

    /// The key of the client in every handshake input (the example of RFC 6455 section 1.3)
    inline constexpr eastl::string_view kHandshakeKey = "dGhlIHNhbXBsZSBub25jZQ==";

}  // namespace BECore::Fuzz::WebSocketInput
