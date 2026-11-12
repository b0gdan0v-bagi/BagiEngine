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

    /// Small helpers for the text of HTTP headers, shared by the server's handshake and the client's (ASCII only)
    [[nodiscard]] eastl::string LowerAscii(eastl::string_view text);
    [[nodiscard]] bool EqualsIgnoreCase(eastl::string_view left, eastl::string_view right);
    /// @p text without the spaces and tabs at both ends
    [[nodiscard]] eastl::string_view TrimHeaderValue(eastl::string_view text);
    /// True when the comma-separated list @p value has the token @p token (any case)
    [[nodiscard]] bool HeaderHasToken(eastl::string_view value, eastl::string_view token);

    /// How much of a text of the peer an error message repeats
    inline constexpr size_t kMaxQuotedBytes = 100;

    /// @p text as an error message may repeat it: printable ASCII only (anything else is `?`), cut to kMaxQuotedBytes with `...` after
    [[nodiscard]] eastl::string QuoteForMessage(eastl::string_view text);

    /// `Sec-WebSocket-Accept` for @p clientKey: base64(SHA-1(key + GUID))
    [[nodiscard]] eastl::string ComputeAcceptKey(eastl::string_view clientKey);

    /// Who may open a connection. The default is the control API's: a program on this machine, or a page of the loopback
    struct HandshakePolicy {
        /// True: `Host` has to name the loopback and `Origin`, when there is one, has to be a page of the loopback; the
        /// other fields are not used. False: a public server, `Host` is not checked and `Origin` goes through allowedOrigins
        bool loopbackOnly = true;

        /// Public mode: the pages that may connect, each `scheme://host[:port]` (no path; see IsOriginAllowed()). The entry
        /// `*` allows every origin (for development only), `null` allows the `null` origin. An empty list refuses every browser
        /// and admits programs, which send no `Origin`
        eastl::vector<eastl::string> allowedOrigins;

        /// Public mode: false refuses a request without `Origin` too, which keeps programs out
        bool allowMissingOrigin = true;

        /// Public mode: the path of a plain `GET` that a hosting platform's health check sends (`/healthz`); IsHealthCheckRequest()
        /// answers it with a 200 instead of a handshake error. Empty is off. Not used when loopbackOnly is true, so the control API
        /// server never answers it
        eastl::string healthPath;
    };

    /**
     * @brief Whether a request that carries the `Origin` header @p origin may connect under @p policy.
     *
     * Loopback mode: an http(s) page of the loopback (a path after the authority is tolerated). Public mode: @p origin
     * has to equal an entry of the allow-list, the entry `*` allows everything, the `null` origin (a `file://` page, a
     * sandboxed frame) needs the entry `null`. Both sides are parsed as `scheme://host[:port]` and compared as parsed:
     *
     * - scheme and host in any case; the port as a number, and an absent port is the default of `http` (80) or `https`
     *   (443), so `https://a.com` equals `https://a.com:443`; other schemes have no default port;
     * - nothing else is allowed after the authority but an empty path or `/`; a `userinfo@`, a space, a control character, a
     *   host with a character a name cannot have, an empty or zero port, a port above 65535: the origin does not match
     *   (an entry like that matches nothing, so it is harmless but useless);
     * - there is no suffix or wildcard matching: `https://good.com.evil.org` is not `https://good.com`, and a trailing dot
     *   of the host (`a.com.`) is another host.
     *
     * Not for a missing header: that is HandshakePolicy::allowMissingOrigin.
     */
    [[nodiscard]] bool IsOriginAllowed(eastl::string_view origin, const HandshakePolicy& policy);

    /**
     * @brief Checks the HTTP request of a WebSocket opening handshake and returns its `Sec-WebSocket-Key`.
     *
     * @p requestText is the request head, up to the empty line. Besides RFC 6455 (GET, HTTP/1.1, `Upgrade: websocket`,
     * `Connection: Upgrade`, version 13, a 16-byte base64 key) the request is refused when it could come from a web page
     * that @p policy does not admit. With the default policy (a development tool on localhost):
     *
     * - `Host` has to name the loopback (`127.0.0.1`, `localhost`, `[::1]`, with or without a port): a name that
     *   resolves to 127.0.0.1 from elsewhere (DNS rebinding) is refused;
     * - `Origin`, when there is one, has to be an http(s) page of the loopback; programs do not send it.
     *
     * With a public policy `Host` only has to be there (the public name is not known behind a proxy), and `Origin`
     * goes through IsOriginAllowed(); a missing one is allowed unless the policy says otherwise.
     *
     * Status codes: 400 for a malformed request, 403 for a foreign `Host` or `Origin` (the message of a public refusal
     * names the origin, cut to 100 bytes), 426 for another protocol version, 431 for a head over kMaxHandshakeBytes.
     */
    [[nodiscard]] std::expected<eastl::string, HandshakeError> ParseHandshakeRequest(eastl::string_view requestText, const HandshakePolicy& policy = HandshakePolicy{});

    /**
     * @brief Whether the request head @p requestText is a health check under @p policy: `GET <healthPath>` (an optional
     * `?query` is ignored) over HTTP/1.1 or HTTP/1.0, with no `Upgrade` header.
     *
     * A request with `Upgrade` is a handshake like any other ("any path opens the same endpoint"), so a client that asks for a
     * WebSocket on the health path still gets one. False when the policy is loopback-only or has no health path. `Host` and
     * `Origin` are not looked at: a platform's probe sends no page origin and its own host name.
     */
    [[nodiscard]] bool IsHealthCheckRequest(eastl::string_view requestText, const HandshakePolicy& policy);

    /// The `200 OK` answer to a health check: body `ok`, not cacheable, and the connection is closed after it
    [[nodiscard]] eastl::string BuildHealthResponse();

    /// The 101 response that completes the handshake for @p clientKey
    [[nodiscard]] eastl::string BuildAcceptResponse(eastl::string_view clientKey);

    /// An HTTP error response for @p error (it closes the connection; a 426 names the version the server speaks)
    [[nodiscard]] eastl::string BuildRejectResponse(const HandshakeError& error);

}  // namespace BECore
