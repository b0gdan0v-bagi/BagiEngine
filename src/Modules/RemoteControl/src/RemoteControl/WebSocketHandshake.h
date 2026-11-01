#pragma once

namespace BECore {

    /// The GUID that RFC 6455 (section 4.2.2) appends to the client key before hashing
    inline constexpr eastl::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    /// Longest request header the server reads while waiting for the end of the handshake
    inline constexpr size_t kMaxHandshakeBytes = 8 * 1024;

    /// Why a handshake is refused: the HTTP status to answer with and a line for the person who sees it
    struct HandshakeError {
        int httpStatus = 400;
        eastl::string message;
    };

    /// `Sec-WebSocket-Accept` for @p clientKey: base64(SHA-1(key + GUID))
    [[nodiscard]] eastl::string ComputeAcceptKey(eastl::string_view clientKey);

    /**
     * @brief Checks the HTTP request of a WebSocket opening handshake and returns its `Sec-WebSocket-Key`.
     *
     * @p requestText is the request head, up to the empty line. The server is a development tool on localhost, so
     * besides RFC 6455 (GET, HTTP/1.1, `Upgrade: websocket`, `Connection: Upgrade`, version 13, a 16-byte base64 key)
     * the request is refused when it could come from a web page that is not on this machine:
     *
     * - `Host` has to name the loopback (`127.0.0.1`, `localhost`, `[::1]`, with or without a port): a name that
     *   resolves to 127.0.0.1 from elsewhere (DNS rebinding) is refused;
     * - `Origin`, when there is one, has to be an http(s) page of the loopback; programs do not send it.
     *
     * Status codes: 400 for a malformed request, 403 for a foreign `Host` or `Origin`, 426 for another protocol
     * version, 431 for a head over kMaxHandshakeBytes.
     */
    [[nodiscard]] std::expected<eastl::string, HandshakeError> ParseHandshakeRequest(eastl::string_view requestText);

    /// The 101 response that completes the handshake for @p clientKey
    [[nodiscard]] eastl::string BuildAcceptResponse(eastl::string_view clientKey);

    /// An HTTP error response for @p error (it closes the connection; a 426 names the version the server speaks)
    [[nodiscard]] eastl::string BuildRejectResponse(const HandshakeError& error);

}  // namespace BECore
