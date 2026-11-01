#include "WebSocketHandshake.h"

#include <BECore/Utils/Base64.h>
#include <RemoteControl/Sha1.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kSupportedVersion = "13";
        constexpr eastl::string_view kRequestLinePrefix = "GET ";
        constexpr eastl::string_view kRequestLineSuffix = " HTTP/1.1";
        constexpr size_t kClientKeyLength = 24;  // 16 random bytes in base64 with padding
        constexpr size_t kMaxPortDigits = 5;

        namespace HeaderNames {
            constexpr eastl::string_view kHost = "host";
            constexpr eastl::string_view kOrigin = "origin";
            constexpr eastl::string_view kUpgrade = "upgrade";
            constexpr eastl::string_view kConnection = "connection";
            constexpr eastl::string_view kKey = "sec-websocket-key";
            constexpr eastl::string_view kVersion = "sec-websocket-version";
        }  // namespace HeaderNames

        using Headers = eastl::hash_map<eastl::string, eastl::string>;

        char AsciiLower(char letter) {
            return (letter >= 'A' && letter <= 'Z') ? static_cast<char>(letter - 'A' + 'a') : letter;
        }

        eastl::string Lowered(eastl::string_view text) {
            eastl::string lowered;
            lowered.reserve(text.size());
            for (const char letter : text) {
                lowered.push_back(AsciiLower(letter));
            }
            return lowered;
        }

        bool EqualsIgnoreCase(eastl::string_view left, eastl::string_view right) {
            return left.size() == right.size() && Lowered(left) == Lowered(right);
        }

        eastl::string_view Trimmed(eastl::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
                text.remove_suffix(1);
            }
            return text;
        }

        HandshakeError Refuse(int status, eastl::string message) {
            return HandshakeError{status, std::move(message)};
        }

        /// True when the comma-separated list @p value has the token @p token (any case)
        bool HasToken(eastl::string_view value, eastl::string_view token) {
            while (!value.empty()) {
                const size_t comma = value.find(',');
                const eastl::string_view item = Trimmed(value.substr(0, comma));
                if (EqualsIgnoreCase(item, token)) {
                    return true;
                }
                if (comma == eastl::string_view::npos) {
                    break;
                }
                value.remove_prefix(comma + 1);
            }
            return false;
        }

        bool IsPort(eastl::string_view text) {
            if (text.empty() || text.size() > kMaxPortDigits) {
                return false;
            }
            return std::all_of(text.begin(), text.end(), [](char digit) { return digit >= '0' && digit <= '9'; });
        }

        /// `127.0.0.1`, `localhost` or `[::1]`, each optionally followed by `:port` (the form of a Host header and of an Origin authority)
        bool IsLoopbackAuthority(eastl::string_view authority) {
            eastl::string_view host = authority;
            if (authority.starts_with('[')) {
                const size_t close = authority.find(']');
                if (close == eastl::string_view::npos) {
                    return false;
                }
                host = authority.substr(0, close + 1);
                const eastl::string_view rest = authority.substr(close + 1);
                if (!rest.empty() && !(rest.starts_with(':') && IsPort(rest.substr(1)))) {
                    return false;
                }
            } else {
                const size_t colon = authority.find(':');
                if (colon != eastl::string_view::npos) {
                    if (!IsPort(authority.substr(colon + 1))) {
                        return false;
                    }
                    host = authority.substr(0, colon);
                }
            }
            return EqualsIgnoreCase(host, "127.0.0.1") || EqualsIgnoreCase(host, "localhost") || host == "[::1]";
        }

        /// `http://127.0.0.1:3000` and `https://localhost`; a path after the authority is tolerated. The `null` origin of a file page is not loopback.
        bool IsLoopbackOrigin(eastl::string_view origin) {
            eastl::string_view rest;
            if (origin.starts_with("http://")) {
                rest = origin.substr(7);
            } else if (origin.starts_with("https://")) {
                rest = origin.substr(8);
            } else {
                return false;
            }
            return IsLoopbackAuthority(rest.substr(0, rest.find('/')));
        }

        bool IsBase64Character(char letter) {
            return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '+' || letter == '/';
        }

        /// The shape of 16 bytes in base64: 22 characters and "=="
        bool IsClientKey(eastl::string_view key) {
            if (key.size() != kClientKeyLength || !key.ends_with("==")) {
                return false;
            }
            return std::all_of(key.begin(), key.end() - 2, IsBase64Character);
        }

        /// Reads the header lines after the request line into @p headers (names in lower case); a repeated header is joined only for the two list headers
        std::optional<HandshakeError> ReadHeaders(eastl::string_view lines, Headers& headers) {
            while (!lines.empty()) {
                const size_t end = lines.find('\n');
                eastl::string_view line = lines.substr(0, end);
                lines = (end == eastl::string_view::npos) ? eastl::string_view() : lines.substr(end + 1);
                if (line.ends_with('\r')) {
                    line.remove_suffix(1);
                }
                if (line.empty()) {
                    break;  // the end of the head
                }
                if (line.front() == ' ' || line.front() == '\t') {
                    return Refuse(400, "folded header lines are not accepted");
                }

                const size_t colon = line.find(':');
                if (colon == eastl::string_view::npos || colon == 0) {
                    return Refuse(400, "a header line has no name or no colon");
                }
                const eastl::string name = Lowered(line.substr(0, colon));
                const eastl::string_view value = Trimmed(line.substr(colon + 1));

                auto found = headers.find(name);
                if (found == headers.end()) {
                    headers.emplace(name, eastl::string(value.data(), value.size()));
                } else if (name == HeaderNames::kUpgrade || name == HeaderNames::kConnection) {
                    found->second.append(",");
                    found->second.append(value.data(), value.size());
                } else if (name == HeaderNames::kHost || name == HeaderNames::kOrigin || name == HeaderNames::kKey || name == HeaderNames::kVersion) {
                    return Refuse(400, Format("the header '{}' is repeated", name));
                }
            }
            return std::nullopt;
        }

        const eastl::string* FindHeader(const Headers& headers, eastl::string_view name) {
            const auto found = headers.find(eastl::string(name.data(), name.size()));
            return found == headers.end() ? nullptr : &found->second;
        }

        eastl::string_view ReasonPhrase(int status) {
            switch (status) {
                case 101:
                    return "Switching Protocols";
                case 403:
                    return "Forbidden";
                case 426:
                    return "Upgrade Required";
                case 431:
                    return "Request Header Fields Too Large";
                case 503:
                    return "Service Unavailable";
                default:
                    return "Bad Request";
            }
        }

    }  // namespace

    eastl::string ComputeAcceptKey(eastl::string_view clientKey) {
        eastl::string joined(clientKey.data(), clientKey.size());
        joined.append(kWebSocketGuid.data(), kWebSocketGuid.size());
        const Sha1Digest digest = Sha1(eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(joined.data()), joined.size()));
        return Base64Encode(eastl::span<const uint8_t>(digest.data(), digest.size()));
    }

    std::expected<eastl::string, HandshakeError> ParseHandshakeRequest(eastl::string_view requestText) {
        if (requestText.size() > kMaxHandshakeBytes) {
            return std::unexpected(Refuse(431, "the request head is too long"));
        }

        const size_t requestLineEnd = requestText.find('\n');
        eastl::string_view requestLine = requestText.substr(0, requestLineEnd);
        if (requestLine.ends_with('\r')) {
            requestLine.remove_suffix(1);
        }
        const eastl::string_view headerLines = (requestLineEnd == eastl::string_view::npos) ? eastl::string_view() : requestText.substr(requestLineEnd + 1);

        // "GET <target> HTTP/1.1": the target is not used, any path opens the same endpoint
        if (!requestLine.starts_with(kRequestLinePrefix) || !requestLine.ends_with(kRequestLineSuffix) || requestLine.size() <= kRequestLinePrefix.size() + kRequestLineSuffix.size()) {
            return std::unexpected(Refuse(400, "expected a GET request over HTTP/1.1"));
        }

        Headers headers;
        if (auto error = ReadHeaders(headerLines, headers)) {
            return std::unexpected(std::move(*error));
        }

        // Who is asking comes first: a refusal for a foreign page says nothing about the protocol
        const eastl::string* host = FindHeader(headers, HeaderNames::kHost);
        if (host == nullptr) {
            return std::unexpected(Refuse(400, "the Host header is missing"));
        }
        if (!IsLoopbackAuthority(*host)) {
            return std::unexpected(Refuse(403, Format("the Host '{}' is not the loopback", *host)));
        }
        if (const eastl::string* origin = FindHeader(headers, HeaderNames::kOrigin)) {
            if (!IsLoopbackOrigin(*origin)) {
                return std::unexpected(Refuse(403, Format("the Origin '{}' is not a page of the loopback", *origin)));
            }
        }

        const eastl::string* upgrade = FindHeader(headers, HeaderNames::kUpgrade);
        if (upgrade == nullptr || !HasToken(*upgrade, "websocket")) {
            return std::unexpected(Refuse(400, "expected 'Upgrade: websocket'"));
        }
        const eastl::string* connection = FindHeader(headers, HeaderNames::kConnection);
        if (connection == nullptr || !HasToken(*connection, "upgrade")) {
            return std::unexpected(Refuse(400, "expected 'Connection: Upgrade'"));
        }
        const eastl::string* version = FindHeader(headers, HeaderNames::kVersion);
        if (version == nullptr || *version != kSupportedVersion) {
            return std::unexpected(Refuse(426, "this server speaks WebSocket version 13"));
        }
        const eastl::string* key = FindHeader(headers, HeaderNames::kKey);
        if (key == nullptr || !IsClientKey(*key)) {
            return std::unexpected(Refuse(400, "'Sec-WebSocket-Key' is missing or is not 16 bytes in base64"));
        }
        return *key;
    }

    eastl::string BuildAcceptResponse(eastl::string_view clientKey) {
        return Format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {}\r\n\r\n", ComputeAcceptKey(clientKey));
    }

    eastl::string BuildRejectResponse(const HandshakeError& error) {
        const eastl::string_view versionHeader = (error.httpStatus == 426) ? "Sec-WebSocket-Version: 13\r\n" : "";
        return Format("HTTP/1.1 {} {}\r\nConnection: close\r\n{}Content-Type: text/plain\r\nContent-Length: {}\r\n\r\n{}", error.httpStatus, ReasonPhrase(error.httpStatus), versionHeader,
                      error.message.size(), error.message);
    }

}  // namespace BECore
