#pragma once

#include "WebSocketUrl.h"

#include <random>

namespace BECore {

    /// A random `Sec-WebSocket-Key`: 16 bytes drawn from @p random, in Base64 (24 characters)
    [[nodiscard]] String GenerateClientKey(std::random_device& random);

    /**
     * @brief The opening request of a client (RFC 6455, section 4.1), up to and including the empty line.
     *
     * `Origin` is written only when @p origin is not empty; it comes from the program's settings and must not hold a
     * line break. A native client sends none (it is not a page); the server's allow-list is about pages.
     */
    [[nodiscard]] String BuildClientHandshakeRequest(const WebSocketUrl& url, StringView key, StringView origin = {});

    /**
     * @brief Checks the head of the server's answer to a request made with @p key.
     *
     * @p responseHead is the text up to the empty line. Accepted only: status 101, `Upgrade` with the token websocket,
     * `Connection` with the token upgrade, `Sec-WebSocket-Accept` equal to ComputeAcceptKey(@p key), and neither
     * `Sec-WebSocket-Extensions` nor `Sec-WebSocket-Protocol` (the request asked for none). The error is a message
     * for the log (a refusal names the status the server answered with).
     */
    [[nodiscard]] std::expected<void, String> CheckServerHandshakeResponse(StringView responseHead, StringView key);

}  // namespace BECore
