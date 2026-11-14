#include "WebSocketUrl.h"

#include "WebSocketHandshake.h"

namespace BECore {

    namespace {

        constexpr StringView kSchemeSeparator = "://";
        constexpr size_t kMaxPortDigits = 5;
        constexpr uint32_t kMaxPort = 65535;
        constexpr char kLastControlCharacter = ' ';  // every byte up to and including the space is refused
        constexpr char kDelete = 0x7F;

        bool IsForbiddenByte(char letter) {
            return letter <= kLastControlCharacter || letter >= kDelete;  // char is signed: a byte over 0x7F is negative
        }

        bool IsHostCharacter(char letter) {
            return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '-' || letter == '.' || letter == '_';
        }

        std::unexpected<String> Refuse(String message) {
            return std::unexpected(std::move(message));
        }

        /// The port in @p digits, nullopt when it is not 1..65535
        Optional<uint16_t> ParsePort(StringView digits) {
            if (digits.empty() || digits.size() > kMaxPortDigits) {
                return std::nullopt;
            }
            uint32_t value = 0;
            for (const char digit : digits) {
                if (digit < '0' || digit > '9') {
                    return std::nullopt;
                }
                value = value * 10 + static_cast<uint32_t>(digit - '0');
            }
            if (value == 0 || value > kMaxPort) {
                return std::nullopt;
            }
            return static_cast<uint16_t>(value);
        }

    }  // namespace

    std::expected<WebSocketUrl, String> ParseWebSocketUrl(StringView text) {
        if (text.size() > kMaxWebSocketUrlBytes) {
            return Refuse(Format("the URL is longer than {} bytes", kMaxWebSocketUrlBytes));
        }
        if (std::any_of(text.begin(), text.end(), IsForbiddenByte)) {
            return Refuse("the URL has a space, a control character or a non-ASCII byte");
        }
        if (text.find('#') != StringView::npos) {
            return Refuse("the URL has a fragment");
        }

        const size_t separator = text.find(kSchemeSeparator);
        if (separator == StringView::npos) {
            return Refuse("the URL has no scheme (expected ws:// or wss://)");
        }
        WebSocketUrl url;
        const String scheme = LowerAscii(text.substr(0, separator));
        if (scheme == "wss") {
            url.secure = true;
        } else if (scheme != "ws") {
            return Refuse(Format("the scheme '{}' is not ws or wss", QuoteForMessage(scheme)));
        }

        const StringView rest = text.substr(separator + kSchemeSeparator.size());
        const size_t authorityEnd = rest.find_first_of("/?");
        const StringView authority = rest.substr(0, authorityEnd);
        if (authority.find('@') != StringView::npos) {
            return Refuse("the URL has userinfo before the host");
        }
        if (authority.starts_with('[')) {
            return Refuse("IPv6 is not supported");
        }

        StringView host = authority;
        url.port = url.secure ? kDefaultSecureWebSocketPort : kDefaultWebSocketPort;
        const size_t colon = authority.find(':');
        if (colon != StringView::npos) {
            host = authority.substr(0, colon);
            const Optional<uint16_t> port = ParsePort(authority.substr(colon + 1));
            if (!port) {
                return Refuse("the port has to be a number from 1 to 65535");
            }
            url.port = *port;
        }
        if (host.empty()) {
            return Refuse("the URL has no host");
        }
        if (host.size() > kMaxWebSocketHostBytes) {
            return Refuse(Format("the host is longer than {} bytes", kMaxWebSocketHostBytes));
        }
        if (!std::all_of(host.begin(), host.end(), IsHostCharacter)) {
            return Refuse("the host has a character a name cannot have");
        }
        url.host = LowerAscii(host);

        if (authorityEnd == StringView::npos) {
            url.pathAndQuery = "/";
        } else {
            const StringView tail = rest.substr(authorityEnd);
            url.pathAndQuery = tail.starts_with('/') ? String(tail.data(), tail.size()) : Format("/{}", tail);  // `ws://a.com?x` asks for `/?x`
        }
        return url;
    }

    String GetHostHeader(const WebSocketUrl& url) {
        const uint16_t defaultPort = url.secure ? kDefaultSecureWebSocketPort : kDefaultWebSocketPort;
        return url.port == defaultPort ? url.host : Format("{}:{}", url.host, url.port);
    }

}  // namespace BECore
