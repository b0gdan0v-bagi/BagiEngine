#include "FuzzTargets.h"
#include "FuzzWebSocketInput.h"

#include <RemoteControl/WebSocketClientHandshake.h>
#include <RemoteControl/WebSocketUrl.h>

namespace BECore::Fuzz {

    namespace {

        using WebSocketInput::HandshakePolicyKind;

        constexpr eastl::string_view kHealthPath = "/healthz";
        constexpr eastl::string_view kHeadEnd = "\r\n\r\n";

        /// The statuses ParseHandshakeRequest documents
        bool IsKnownRefusalStatus(int status) {
            return status == 400 || status == 403 || status == 426 || status == 431;
        }

        HandshakePolicy MakePolicy(HandshakePolicyKind kind) {
            HandshakePolicy policy;
            switch (kind) {
                case HandshakePolicyKind::Loopback:
                    break;
                case HandshakePolicyKind::PublicWithOrigins:
                    policy.loopbackOnly = false;
                    policy.allowedOrigins = {"https://good.example", "http://localhost:8080", "null"};
                    break;
                case HandshakePolicyKind::PublicAnyOrigin:
                    policy.loopbackOnly = false;
                    policy.allowedOrigins = {"*"};
                    break;
                case HandshakePolicyKind::PublicNoMissingOrigin:
                    policy.loopbackOnly = false;
                    policy.allowedOrigins = {"https://good.example"};
                    policy.allowMissingOrigin = false;
                    break;
                default:  // PublicWithHealthPath
                    policy.loopbackOnly = false;
                    policy.allowedOrigins = {"https://good.example"};
                    policy.healthPath = eastl::string(kHealthPath.data(), kHealthPath.size());
                    break;
            }
            return policy;
        }

        /// The server's half: the bytes as the head of a request
        void CheckRequest(eastl::string_view text, const HandshakePolicy& policy) {
            const auto parsed = ParseHandshakeRequest(text, policy);
            const bool isHealthCheck = IsHealthCheckRequest(text, policy);
            if (isHealthCheck) {
                // A health check is answered before the handshake and only by a policy that has a path; a request with Upgrade is not one
                FUZZ_CHECK(!policy.loopbackOnly);
                FUZZ_CHECK(!policy.healthPath.empty());
                FUZZ_CHECK(!parsed.has_value());
            }
            if (!parsed.has_value()) {
                FUZZ_CHECK(IsKnownRefusalStatus(parsed.error().httpStatus));
                FUZZ_CHECK(!parsed.error().message.empty());
                const eastl::string reject = BuildRejectResponse(parsed.error());
                FUZZ_CHECK(eastl::string_view(reject).starts_with("HTTP/1.1 "));
                FUZZ_CHECK(!CheckServerHandshakeResponse(reject, WebSocketInput::kHandshakeKey).has_value());  // a refusal is never an accepted answer
                return;
            }

            // An accepted request: the accept response that the server builds for its key is the one a client of that key accepts, and no other key does
            const eastl::string key = *parsed;
            FUZZ_CHECK(key.size() == 24);
            FUZZ_CHECK(!isHealthCheck);
            const eastl::string accept = BuildAcceptResponse(key);
            FUZZ_CHECK(CheckServerHandshakeResponse(accept, key).has_value());
            eastl::string otherKey = key;
            otherKey[0] = (otherKey[0] == 'A') ? 'B' : 'A';
            FUZZ_CHECK(!CheckServerHandshakeResponse(accept, otherKey).has_value());
        }

        /// The client's half: the bytes as the head of a response
        void CheckResponse(eastl::string_view text) {
            const auto checked = CheckServerHandshakeResponse(text, WebSocketInput::kHandshakeKey);
            if (!checked.has_value()) {
                FUZZ_CHECK(!checked.error().empty());
                return;
            }
            // Accepted: nothing but a well-formed 101 for this key can be, so the answer must name the right accept value
            const eastl::string accept = ComputeAcceptKey(WebSocketInput::kHandshakeKey);
            FUZZ_CHECK(text.find(accept.c_str()) != eastl::string_view::npos);
            FUZZ_CHECK(text.starts_with("HTTP/1.1 101"));
        }

        /// The Host header of a request for @p url names the same address as the URL
        void CheckUrl(eastl::string_view text) {
            const auto parsed = ParseWebSocketUrl(text);
            if (!parsed.has_value()) {
                FUZZ_CHECK(!parsed.error().empty());
                return;
            }
            const WebSocketUrl& url = *parsed;
            FUZZ_CHECK(!url.host.empty());
            FUZZ_CHECK(url.port >= 1);
            FUZZ_CHECK(!url.pathAndQuery.empty() && url.pathAndQuery[0] == '/');

            const eastl::string hostHeader = GetHostHeader(url);
            const eastl::string again = Format("{}://{}{}", url.secure ? "wss" : "ws", hostHeader, url.pathAndQuery);
            const auto reparsed = ParseWebSocketUrl(again);
            FUZZ_CHECK(reparsed.has_value());
            FUZZ_CHECK(reparsed->secure == url.secure);
            FUZZ_CHECK(reparsed->host == url.host);
            FUZZ_CHECK(reparsed->port == url.port);
            FUZZ_CHECK(reparsed->pathAndQuery == url.pathAndQuery);

            // The request a client makes for it is one a public server admits (no Origin is written), and it carries the key through
            const eastl::string request = BuildClientHandshakeRequest(url, WebSocketInput::kHandshakeKey);
            HandshakePolicy open;
            open.loopbackOnly = false;
            const auto accepted = ParseHandshakeRequest(request, open);
            FUZZ_CHECK(accepted.has_value());
            FUZZ_CHECK(*accepted == WebSocketInput::kHandshakeKey);
        }

    }  // namespace

    void FuzzHandshake(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        const auto kind = static_cast<HandshakePolicyKind>(data[0] % EnumUtils<HandshakePolicyKind>::Count());
        const eastl::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);

        CheckRequest(text, MakePolicy(kind));
        CheckResponse(text);
        CheckUrl(text);
    }

}  // namespace BECore::Fuzz
