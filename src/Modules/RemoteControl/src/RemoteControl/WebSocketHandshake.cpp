#include "WebSocketHandshake.h"

#include <BECore/Utils/Base64.h>
#include <RemoteControl/Sha1.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kSupportedVersion = "13";
        constexpr eastl::string_view kRequestLinePrefix = "GET ";
        constexpr eastl::string_view kRequestLineSuffix = " HTTP/1.1";
        constexpr eastl::string_view kRequestLineSuffixHttp10 = " HTTP/1.0";
        constexpr eastl::string_view kHealthBody = "ok";
        constexpr size_t kClientKeyLength = 24;  // 16 random bytes in base64 with padding
        constexpr size_t kMaxPortDigits = 5;
        constexpr uint32_t kMaxPort = 65535;
        constexpr int kNoPort = -1;
        constexpr int kDefaultHttpPort = 80;
        constexpr int kDefaultHttpsPort = 443;
        constexpr eastl::string_view kSchemeSeparator = "://";
        constexpr eastl::string_view kAnyOrigin = "*";
        constexpr eastl::string_view kNullOrigin = "null";

        namespace HeaderNames {
            constexpr eastl::string_view kHost = "host";
            constexpr eastl::string_view kOrigin = "origin";
            constexpr eastl::string_view kUpgrade = "upgrade";
            constexpr eastl::string_view kConnection = "connection";
            constexpr eastl::string_view kKey = "sec-websocket-key";
            constexpr eastl::string_view kVersion = "sec-websocket-version";
            constexpr eastl::string_view kForwardedFor = "x-forwarded-for";
        }  // namespace HeaderNames

        using Headers = eastl::hash_map<eastl::string, eastl::string>;

        char AsciiLower(char letter) {
            return (letter >= 'A' && letter <= 'Z') ? static_cast<char>(letter - 'A' + 'a') : letter;
        }

        HandshakeError Refuse(int status, eastl::string message) {
            return HandshakeError{status, std::move(message)};
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

        /// An origin split into the parts that are compared: `scheme://host[:port]`
        struct ParsedOrigin {
            eastl::string scheme;  // lower case
            eastl::string host;    // lower case, `[v6]` keeps its brackets
            int port = kNoPort;    // the port written, or the default of the scheme, or kNoPort
        };

        bool IsSchemeCharacter(char letter, bool first) {
            const char lower = AsciiLower(letter);
            if (lower >= 'a' && lower <= 'z') {
                return true;
            }
            return !first && ((letter >= '0' && letter <= '9') || letter == '+' || letter == '-' || letter == '.');
        }

        bool IsHostCharacter(char letter) {
            const char lower = AsciiLower(letter);
            return (lower >= 'a' && lower <= 'z') || (letter >= '0' && letter <= '9') || letter == '-' || letter == '.';
        }

        /// A registered name, an IPv4 address or an IPv6 literal in brackets (hex digits, ':' and '.'); never empty
        bool IsOriginHost(eastl::string_view host) {
            if (host.empty()) {
                return false;
            }
            if (host.front() == '[') {
                if (host.size() < 3 || host.back() != ']') {
                    return false;
                }
                const eastl::string_view inside = host.substr(1, host.size() - 2);
                return std::all_of(inside.begin(), inside.end(), [](char letter) {
                    const char lower = AsciiLower(letter);
                    return (lower >= 'a' && lower <= 'f') || (letter >= '0' && letter <= '9') || letter == ':' || letter == '.';
                });
            }
            return std::all_of(host.begin(), host.end(), IsHostCharacter);
        }

        int DefaultPortOf(eastl::string_view scheme) {
            if (scheme == "http") {
                return kDefaultHttpPort;
            }
            return scheme == "https" ? kDefaultHttpsPort : kNoPort;
        }

        /// Parses `scheme://host[:port]` with an optional single trailing `/`; nullopt for anything else
        Optional<ParsedOrigin> ParseOrigin(eastl::string_view text) {
            const size_t separator = text.find(kSchemeSeparator);
            if (separator == eastl::string_view::npos || separator == 0) {
                return std::nullopt;
            }
            const eastl::string_view scheme = text.substr(0, separator);
            for (size_t i = 0; i < scheme.size(); ++i) {
                if (!IsSchemeCharacter(scheme[i], i == 0)) {
                    return std::nullopt;
                }
            }

            eastl::string_view authority = text.substr(separator + kSchemeSeparator.size());
            if (authority.ends_with('/')) {
                authority.remove_suffix(1);  // `https://a.com/` is the same origin; `https://a.com/x` is not an origin at all
            }

            eastl::string_view host = authority;
            eastl::string_view port;
            bool hasPort = false;
            const size_t hostEnd = authority.starts_with('[') ? authority.find(']') : authority.find(':');
            if (authority.starts_with('[')) {
                if (hostEnd == eastl::string_view::npos) {
                    return std::nullopt;
                }
                host = authority.substr(0, hostEnd + 1);
                const eastl::string_view rest = authority.substr(hostEnd + 1);
                if (!rest.empty()) {
                    if (!rest.starts_with(':')) {
                        return std::nullopt;
                    }
                    port = rest.substr(1);
                    hasPort = true;
                }
            } else if (hostEnd != eastl::string_view::npos) {
                host = authority.substr(0, hostEnd);
                port = authority.substr(hostEnd + 1);
                hasPort = true;
            }
            if (!IsOriginHost(host)) {
                return std::nullopt;  // also catches a path, `userinfo@`, spaces, control characters and a second ':'
            }

            ParsedOrigin parsed;
            parsed.scheme = LowerAscii(scheme);
            parsed.host = LowerAscii(host);
            parsed.port = DefaultPortOf(parsed.scheme);
            if (hasPort) {
                if (!IsPort(port)) {
                    return std::nullopt;
                }
                uint32_t value = 0;
                for (const char digit : port) {
                    value = value * 10 + static_cast<uint32_t>(digit - '0');
                }
                if (value == 0 || value > kMaxPort) {
                    return std::nullopt;
                }
                parsed.port = static_cast<int>(value);
            }
            return parsed;
        }

        bool SameOrigin(const ParsedOrigin& left, const ParsedOrigin& right) {
            return left.scheme == right.scheme && left.host == right.host && left.port == right.port;
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

        /// Reads the header lines after the request line into @p headers (names in lower case); a repeated header is joined only for the list headers
        Optional<HandshakeError> ReadHeaders(eastl::string_view lines, Headers& headers) {
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
                const eastl::string name = LowerAscii(line.substr(0, colon));
                const eastl::string_view value = TrimHeaderValue(line.substr(colon + 1));

                auto found = headers.find(name);
                if (found == headers.end()) {
                    headers.emplace(name, eastl::string(value.data(), value.size()));
                } else if (name == HeaderNames::kUpgrade || name == HeaderNames::kConnection || name == HeaderNames::kForwardedFor) {
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

        constexpr size_t kIpv4Parts = 4;
        constexpr size_t kMaxIpv4PartDigits = 3;
        constexpr uint32_t kMaxIpv4Part = 255;
        constexpr size_t kMinIpv6Colons = 2;
        constexpr size_t kMaxIpv6Colons = 7;

        bool IsDottedIpv4(eastl::string_view text) {
            size_t parts = 0;
            while (true) {
                const size_t dot = text.find('.');
                const eastl::string_view part = text.substr(0, dot);
                if (part.empty() || part.size() > kMaxIpv4PartDigits) {
                    return false;
                }
                uint32_t value = 0;
                for (const char digit : part) {
                    if (digit < '0' || digit > '9') {
                        return false;
                    }
                    value = value * 10 + static_cast<uint32_t>(digit - '0');
                }
                if (value > kMaxIpv4Part) {
                    return false;
                }
                ++parts;
                if (dot == eastl::string_view::npos) {
                    break;
                }
                text.remove_prefix(dot + 1);
            }
            return parts == kIpv4Parts;
        }

        /// Hex digits, ':' and an optional dotted tail; the shape of a literal, not a full grammar (it is only a key for a limit)
        bool IsIpv6Literal(eastl::string_view text) {
            const auto colons = static_cast<size_t>(std::count(text.begin(), text.end(), ':'));
            if (colons < kMinIpv6Colons || colons > kMaxIpv6Colons) {
                return false;
            }
            return std::all_of(text.begin(), text.end(), [](char letter) {
                const char lower = AsciiLower(letter);
                return (lower >= 'a' && lower <= 'f') || (letter >= '0' && letter <= '9') || letter == ':' || letter == '.';
            });
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

    eastl::string LowerAscii(eastl::string_view text) {
        eastl::string lowered;
        lowered.reserve(text.size());
        for (const char letter : text) {
            lowered.push_back(AsciiLower(letter));
        }
        return lowered;
    }

    bool EqualsIgnoreCase(eastl::string_view left, eastl::string_view right) {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) { return AsciiLower(a) == AsciiLower(b); });
    }

    eastl::string QuoteForMessage(eastl::string_view text) {
        eastl::string quoted;
        quoted.reserve(std::min(text.size(), kMaxQuotedBytes) + 3);
        for (const char letter : text.substr(0, kMaxQuotedBytes)) {
            quoted.push_back(letter >= ' ' && letter <= '~' ? letter : '?');
        }
        if (text.size() > kMaxQuotedBytes) {
            quoted.append("...");
        }
        return quoted;
    }

    eastl::string_view TrimHeaderValue(eastl::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.remove_suffix(1);
        }
        return text;
    }

    bool HeaderHasToken(eastl::string_view value, eastl::string_view token) {
        while (!value.empty()) {
            const size_t comma = value.find(',');
            const eastl::string_view item = TrimHeaderValue(value.substr(0, comma));
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

    eastl::string ComputeAcceptKey(eastl::string_view clientKey) {
        eastl::string joined(clientKey.data(), clientKey.size());
        joined.append(kWebSocketGuid.data(), kWebSocketGuid.size());
        const Sha1Digest digest = Sha1(eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(joined.data()), joined.size()));
        return Base64Encode(eastl::span<const uint8_t>(digest.data(), digest.size()));
    }

    bool IsOriginAllowed(eastl::string_view origin, const HandshakePolicy& policy) {
        if (policy.loopbackOnly) {
            return IsLoopbackOrigin(origin);
        }
        const bool isNull = EqualsIgnoreCase(origin, kNullOrigin);
        const Optional<ParsedOrigin> parsed = isNull ? std::nullopt : ParseOrigin(origin);
        for (const eastl::string& entry : policy.allowedOrigins) {
            const eastl::string_view allowed(entry.data(), entry.size());
            if (allowed == kAnyOrigin) {
                return true;
            }
            if (isNull) {
                if (EqualsIgnoreCase(allowed, kNullOrigin)) {
                    return true;
                }
                continue;
            }
            if (!parsed) {
                continue;  // only `*` can admit an origin that is not well formed
            }
            const Optional<ParsedOrigin> entryOrigin = ParseOrigin(allowed);
            if (entryOrigin && SameOrigin(*parsed, *entryOrigin)) {
                return true;
            }
        }
        return false;
    }

    std::expected<eastl::string, HandshakeError> ParseHandshakeRequest(eastl::string_view requestText, const HandshakePolicy& policy) {
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
        if (policy.loopbackOnly && !IsLoopbackAuthority(*host)) {
            return std::unexpected(Refuse(403, Format("the Host '{}' is not the loopback", QuoteForMessage(*host))));
        }
        if (const eastl::string* origin = FindHeader(headers, HeaderNames::kOrigin)) {
            if (!IsOriginAllowed(*origin, policy)) {
                if (policy.loopbackOnly) {
                    return std::unexpected(Refuse(403, Format("the Origin '{}' is not a page of the loopback", QuoteForMessage(*origin))));
                }
                return std::unexpected(Refuse(403, Format("the Origin '{}' is not allowed by this server", QuoteForMessage(*origin))));
            }
        } else if (!policy.loopbackOnly && !policy.allowMissingOrigin) {
            return std::unexpected(Refuse(403, "this server accepts only requests that carry an Origin header"));
        }

        const eastl::string* upgrade = FindHeader(headers, HeaderNames::kUpgrade);
        if (upgrade == nullptr || !HeaderHasToken(*upgrade, "websocket")) {
            return std::unexpected(Refuse(400, "expected 'Upgrade: websocket'"));
        }
        const eastl::string* connection = FindHeader(headers, HeaderNames::kConnection);
        if (connection == nullptr || !HeaderHasToken(*connection, "upgrade")) {
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

    eastl::string ParseForwardedClientAddress(eastl::string_view requestText, size_t trustedProxyHops) {
        if (trustedProxyHops == 0 || requestText.size() > kMaxHandshakeBytes) {
            return {};
        }
        const size_t requestLineEnd = requestText.find('\n');
        if (requestLineEnd == eastl::string_view::npos) {
            return {};
        }
        Headers headers;
        if (ReadHeaders(requestText.substr(requestLineEnd + 1), headers)) {
            return {};
        }
        const eastl::string* list = FindHeader(headers, HeaderNames::kForwardedFor);
        if (list == nullptr) {
            return {};
        }

        // From the right: the last entry is the one the nearest proxy appended
        eastl::string_view rest(list->data(), list->size());
        for (size_t skipped = 1; skipped < trustedProxyHops; ++skipped) {
            const size_t comma = rest.rfind(',');
            if (comma == eastl::string_view::npos) {
                return {};
            }
            rest = rest.substr(0, comma);
        }
        const size_t comma = rest.rfind(',');
        const eastl::string_view entry = TrimHeaderValue(comma == eastl::string_view::npos ? rest : rest.substr(comma + 1));
        if (entry.empty() || entry.size() > kMaxForwardedAddressBytes || !(IsDottedIpv4(entry) || IsIpv6Literal(entry))) {
            return {};
        }
        return LowerAscii(entry);
    }

    bool IsHealthCheckRequest(eastl::string_view requestText, const HandshakePolicy& policy) {
        if (policy.loopbackOnly || policy.healthPath.empty() || requestText.size() > kMaxHandshakeBytes) {
            return false;
        }

        const size_t requestLineEnd = requestText.find('\n');
        eastl::string_view requestLine = requestText.substr(0, requestLineEnd);
        if (requestLine.ends_with('\r')) {
            requestLine.remove_suffix(1);
        }
        if (!requestLine.starts_with(kRequestLinePrefix)) {
            return false;
        }
        requestLine.remove_prefix(kRequestLinePrefix.size());
        if (requestLine.ends_with(kRequestLineSuffix)) {
            requestLine.remove_suffix(kRequestLineSuffix.size());
        } else if (requestLine.ends_with(kRequestLineSuffixHttp10)) {
            requestLine.remove_suffix(kRequestLineSuffixHttp10.size());
        } else {
            return false;
        }
        const eastl::string_view path = requestLine.substr(0, requestLine.find('?'));
        if (path != eastl::string_view(policy.healthPath.data(), policy.healthPath.size())) {
            return false;
        }

        const eastl::string_view headerLines = (requestLineEnd == eastl::string_view::npos) ? eastl::string_view() : requestText.substr(requestLineEnd + 1);
        Headers headers;
        if (ReadHeaders(headerLines, headers)) {
            return false;  // a malformed head gets the usual 400
        }
        return FindHeader(headers, HeaderNames::kUpgrade) == nullptr;
    }

    eastl::string BuildHealthResponse() {
        return Format("HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: text/plain\r\nContent-Length: {}\r\n\r\n{}", kHealthBody.size(), kHealthBody);
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
