#include "WebSocketClientHandshake.h"

#include "WebSocketHandshake.h"

#include <BECore/Utils/Base64.h>

namespace BECore {

    namespace {

        constexpr size_t kClientKeyBytes = 16;
        constexpr StringView kStatusLinePrefix = "HTTP/1.1 ";
        constexpr size_t kStatusCodeDigits = 3;
        constexpr int kStatusSwitchingProtocols = 101;

        namespace HeaderNames {
            constexpr StringView kUpgrade = "upgrade";
            constexpr StringView kConnection = "connection";
            constexpr StringView kAccept = "sec-websocket-accept";
            constexpr StringView kExtensions = "sec-websocket-extensions";
            constexpr StringView kProtocol = "sec-websocket-protocol";
        }  // namespace HeaderNames

        std::unexpected<String> Refuse(String message) {
            return std::unexpected(std::move(message));
        }

        /// The status code of "HTTP/1.1 101 Switching Protocols", nullopt when the line is not of that shape
        Optional<int> ParseStatus(StringView line) {
            if (!line.starts_with(kStatusLinePrefix)) {
                return std::nullopt;
            }
            const StringView rest = line.substr(kStatusLinePrefix.size());
            if (rest.size() < kStatusCodeDigits || (rest.size() > kStatusCodeDigits && rest[kStatusCodeDigits] != ' ')) {
                return std::nullopt;
            }
            int status = 0;
            for (size_t i = 0; i < kStatusCodeDigits; ++i) {
                if (rest[i] < '0' || rest[i] > '9') {
                    return std::nullopt;
                }
                status = status * 10 + (rest[i] - '0');
            }
            return status;
        }

    }  // namespace

    String GenerateClientKey(std::random_device& random) {
        Array<uint8_t, kClientKeyBytes> bytes{};
        for (uint8_t& byte : bytes) {
            byte = static_cast<uint8_t>(random());
        }
        return Base64Encode(Span<const uint8_t>(bytes.data(), bytes.size()));
    }

    String BuildClientHandshakeRequest(const WebSocketUrl& url, StringView key, StringView origin) {
        String request =
            Format("GET {} HTTP/1.1\r\nHost: {}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {}\r\nSec-WebSocket-Version: 13\r\n", url.pathAndQuery, GetHostHeader(url), key);
        if (!origin.empty()) {
            request.append(Format("Origin: {}\r\n", origin));
        }
        request.append("\r\n");
        return request;
    }

    std::expected<void, String> CheckServerHandshakeResponse(StringView responseHead, StringView key) {
        if (responseHead.size() > kMaxHandshakeBytes) {
            return Refuse("the server's answer is too long");
        }

        StringView lines = responseHead;
        bool isStatusLine = true;
        bool hasUpgrade = false;
        bool hasConnection = false;
        Optional<String> accept;
        while (!lines.empty()) {
            const size_t end = lines.find('\n');
            StringView line = lines.substr(0, end);
            lines = (end == StringView::npos) ? StringView() : lines.substr(end + 1);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            if (isStatusLine) {
                isStatusLine = false;
                const Optional<int> status = ParseStatus(line);
                if (!status) {
                    return Refuse("the server's answer is not an HTTP/1.1 response");
                }
                if (*status != kStatusSwitchingProtocols) {
                    return Refuse(Format("the server refused the connection: {}", QuoteForMessage(line)));
                }
                continue;
            }
            if (line.empty()) {
                break;  // the end of the head
            }
            const size_t colon = line.find(':');
            if (colon == StringView::npos || colon == 0) {
                return Refuse("a header line of the answer has no name or no colon");
            }
            const String name = LowerAscii(line.substr(0, colon));
            const StringView value = TrimHeaderValue(line.substr(colon + 1));
            if (name == HeaderNames::kUpgrade) {
                hasUpgrade = hasUpgrade || HeaderHasToken(value, "websocket");
            } else if (name == HeaderNames::kConnection) {
                hasConnection = hasConnection || HeaderHasToken(value, "upgrade");
            } else if (name == HeaderNames::kAccept) {
                if (accept) {
                    return Refuse("the answer repeats 'Sec-WebSocket-Accept'");
                }
                accept = String(value.data(), value.size());
            } else if (name == HeaderNames::kExtensions || name == HeaderNames::kProtocol) {
                return Refuse(Format("the server answered with '{}', which the request did not ask for", name));
            }
        }

        if (isStatusLine) {
            return Refuse("the server's answer is empty");
        }
        if (!hasUpgrade) {
            return Refuse("the answer lacks 'Upgrade: websocket'");
        }
        if (!hasConnection) {
            return Refuse("the answer lacks 'Connection: Upgrade'");
        }
        if (!accept) {
            return Refuse("the answer lacks 'Sec-WebSocket-Accept'");
        }
        if (*accept != ComputeAcceptKey(key)) {
            return Refuse("'Sec-WebSocket-Accept' does not match the key");
        }
        return {};
    }

}  // namespace BECore
