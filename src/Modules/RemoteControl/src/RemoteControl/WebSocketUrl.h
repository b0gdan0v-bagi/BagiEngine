#pragma once

namespace BECore {

    /// A `ws://` or `wss://` address split into what a client needs to connect
    struct WebSocketUrl {
        bool secure = false;         // wss
        eastl::string host;          // lower case, a name or an IPv4 literal
        uint16_t port = 0;           // written, or the default of the scheme
        eastl::string pathAndQuery;  // starts with '/', the query is kept
    };

    /// Longest URL text ParseWebSocketUrl() takes
    inline constexpr size_t kMaxWebSocketUrlBytes = 2048;

    /// Longest host ParseWebSocketUrl() takes: the longest name DNS has (RFC 1035)
    inline constexpr size_t kMaxWebSocketHostBytes = 253;

    inline constexpr uint16_t kDefaultWebSocketPort = 80;
    inline constexpr uint16_t kDefaultSecureWebSocketPort = 443;

    /**
     * @brief Parses `ws://host[:port][/path][?query]` (the scheme `wss` too; any case).
     *
     * The host is a name or an IPv4 literal; an IPv6 literal in brackets is refused ("IPv6 is not supported", the Socket is
     * IPv4). The port is 1..65535 and defaults to 80 (443 for wss). Refused: another scheme, a fragment, userinfo, a space or
     * a control or non-ASCII byte, an empty host or port, a host over kMaxWebSocketHostBytes, a text over kMaxWebSocketUrlBytes. The error is a message for the log.
     */
    [[nodiscard]] std::expected<WebSocketUrl, eastl::string> ParseWebSocketUrl(StringView text);

    /// The `Host` header value for a request to @p url: the port is left out when it is the default of the scheme
    [[nodiscard]] eastl::string GetHostHeader(const WebSocketUrl& url);

}  // namespace BECore
