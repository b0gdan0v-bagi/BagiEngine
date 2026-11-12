#include "NetworkFrameworkErrors.h"
#include "TlsStream.h"

#ifdef PLATFORM_MACOS

#include <CoreFoundation/CoreFoundation.h>
#include <Network/Network.h>
#include <Security/Security.h>
#include <cerrno>
#include <dispatch/dispatch.h>

namespace BECore {

    namespace {

        constexpr uint32_t kReceiveChunk = 64 * 1024;
        constexpr uint32_t kMinimumConnectTimeoutSeconds = 1;
        constexpr std::chrono::milliseconds kMillisecondsPerSecond{1000};

        /// Releases any object of Network.framework, Security or libdispatch (they are all os_object)
        struct OsObjectReleaser {
            void operator()(void* object) const {
                if (object != nullptr) {
                    os_release(object);
                }
            }
        };

        template <typename T>
        using OsOwned = std::unique_ptr<std::remove_pointer_t<T>, OsObjectReleaser>;

        struct CfReleaser {
            void operator()(CFTypeRef object) const {
                if (object != nullptr) {
                    CFRelease(object);
                }
            }
        };

        template <typename T>
        using CfOwned = std::unique_ptr<std::remove_pointer_t<T>, CfReleaser>;

        eastl::string_view TlsVersionName(tls_protocol_version_t version) {
            switch (version) {
                case tls_protocol_version_TLSv12:
                    return "1.2";
                case tls_protocol_version_TLSv13:
                    return "1.3";
                default:
                    return "of an unexpected version";
            }
        }

        NetworkErrorDomain ToDomain(nw_error_domain_t domain) {
            switch (domain) {
                case nw_error_domain_posix:
                    return NetworkErrorDomain::Posix;
                case nw_error_domain_dns:
                    return NetworkErrorDomain::Dns;
                case nw_error_domain_tls:
                    return NetworkErrorDomain::Tls;
                default:
                    return NetworkErrorDomain::Other;
            }
        }

        enum class Phase : uint8_t { Idle, Connecting, Ready, Failed, Cancelled };

        /**
         * Everything the blocks of Network.framework touch. It lives in a shared_ptr that every block holds, so a callback that
         * arrives after the stream is gone finds it still alive (the stream only cancels the connection; it never waits for it).
         * One mutex guards it all; nothing blocks while it is held.
         */
        class StreamState {
        public:
            ~StreamState() {
                ReleaseConnectionLocked();
            }

            std::mutex mutex;
            std::condition_variable changed;

            Phase phase = Phase::Idle;
            bool closing = false;  // ShutdownBoth() or Close() was called
            eastl::string target;  // host:port, for the texts
            eastl::string failure;
            int verifyFailureCode = 0;  // errSec* of our own trust evaluation, which says more than the connection's error

            nw_connection_t connection = nullptr;

            eastl::vector<uint8_t> inbox;
            size_t inboxRead = 0;
            bool receiveInFlight = false;
            bool endOfStream = false;
            std::chrono::milliseconds receiveTimeout{0};

            uint64_t sendsIssued = 0;
            uint64_t sendsCompleted = 0;
            bool sendFailed = false;

            CfOwned<CFArrayRef> anchors;  // the additional trust anchors, as SecCertificateRef

            /// Cancels the connection and drops our reference to it; the system keeps it alive until the cancel has run
            void ReleaseConnectionLocked() {
                if (connection != nullptr) {
                    nw_connection_cancel(connection);
                    nw_release(connection);
                    connection = nullptr;
                }
            }

            void OnState(nw_connection_state_t state, nw_error_t error) {
                std::lock_guard lock(mutex);
                switch (state) {
                    case nw_connection_state_ready:
                        if (phase == Phase::Connecting) {
                            phase = Phase::Ready;
                        }
                        break;
                    case nw_connection_state_waiting:
                        // A refused connection or a name that does not resolve is reported as "waiting" (the system retries when the
                        // network changes); for a one-shot client that is a failure
                        if (error != nullptr && phase == Phase::Connecting) {
                            FailLocked(error);
                            if (connection != nullptr) {
                                nw_connection_cancel(connection);
                            }
                        }
                        break;
                    case nw_connection_state_failed:
                        FailLocked(error);
                        break;
                    case nw_connection_state_cancelled:
                        if (phase != Phase::Failed) {
                            phase = Phase::Cancelled;
                        }
                        break;
                    default:
                        break;
                }
                changed.notify_all();
            }

            void OnReceived(dispatch_data_t content, nw_content_context_t context, bool isComplete, nw_error_t error) {
                std::lock_guard lock(mutex);
                receiveInFlight = false;
                if (content != nullptr) {
                    dispatch_data_apply(content, ^bool(dispatch_data_t, size_t, const void* bytes, size_t size) {
                      const auto* begin = static_cast<const uint8_t*>(bytes);
                      inbox.insert(inbox.end(), begin, begin + size);
                      return true;
                    });
                }
                if (error != nullptr) {
                    if (!closing) {
                        FailLocked(error);
                    }
                } else if (isComplete && (content == nullptr || (context != nullptr && nw_content_context_get_is_final(context)))) {
                    endOfStream = true;
                }
                changed.notify_all();
            }

            void OnSent(nw_error_t error) {
                std::lock_guard lock(mutex);
                ++sendsCompleted;
                if (error != nullptr) {
                    sendFailed = true;
                    if (!closing) {
                        FailLocked(error);
                    }
                }
                changed.notify_all();
            }

            /// The first failure is the true one; @p error says what failed, our own trust evaluation what was wrong with the certificate
            void FailLocked(nw_error_t error) {
                if (phase == Phase::Failed) {
                    return;
                }
                phase = Phase::Failed;
                NetworkErrorDomain domain = NetworkErrorDomain::Other;
                int code = 0;
                if (error != nullptr) {
                    domain = ToDomain(nw_error_get_error_domain(error));
                    code = nw_error_get_error_code(error);
                }
                if (verifyFailureCode != 0) {
                    domain = NetworkErrorDomain::Tls;
                    code = verifyFailureCode;
                }
                failure = Format("TLS: {}: {}", DescribeNetworkFailure(domain, code), target);
                LOG_WARNING("{} (error domain {}, code {})"_format(failure, EnumUtils<NetworkErrorDomain>::ToString(domain), code));
            }

            /// Called with the mutex held; starts the one pending receive
            void StartReceiveLocked(const std::shared_ptr<StreamState>& self) {
                if (receiveInFlight || connection == nullptr) {
                    return;
                }
                receiveInFlight = true;
                nw_connection_receive(connection, 1, kReceiveChunk, ^(dispatch_data_t content, nw_content_context_t context, bool isComplete, nw_error_t error) {
                  self->OnReceived(content, context, isComplete, error);
                });
            }
        };

        /// The trust evaluation of the verify block (additional anchors only): the chain against the system roots plus the anchors,
        /// the validity and the host name, with the SSL policy. Never returns true without evaluating
        bool EvaluateTrust(StreamState& state, const eastl::string& host, sec_trust_t trust) {
            CfOwned<SecTrustRef> trustRef(sec_trust_copy_ref(trust));
            CfOwned<CFStringRef> hostName(CFStringCreateWithCString(kCFAllocatorDefault, host.c_str(), kCFStringEncodingUTF8));
            if (!trustRef || !hostName) {
                return false;
            }
            CfOwned<SecPolicyRef> policy(SecPolicyCreateSSL(true, hostName.get()));
            if (!policy || SecTrustSetPolicies(trustRef.get(), policy.get()) != errSecSuccess) {
                return false;
            }
            if (SecTrustSetAnchorCertificates(trustRef.get(), state.anchors.get()) != errSecSuccess || SecTrustSetAnchorCertificatesOnly(trustRef.get(), false) != errSecSuccess) {
                return false;
            }
            CFErrorRef rawError = nullptr;
            if (SecTrustEvaluateWithError(trustRef.get(), &rawError)) {
                return true;
            }
            CfOwned<CFErrorRef> evaluationError(rawError);
            std::lock_guard lock(state.mutex);
            state.verifyFailureCode = evaluationError ? static_cast<int>(CFErrorGetCode(evaluationError.get())) : errSecNotTrusted;
            return false;
        }

        /// The anchors as certificates; an empty result with a non-empty input means a certificate was not valid DER (the caller then refuses to connect)
        CfOwned<CFArrayRef> MakeAnchors(eastl::span<const eastl::vector<uint8_t>> anchorsDer) {
            CfOwned<CFMutableArrayRef> array(CFArrayCreateMutable(kCFAllocatorDefault, static_cast<CFIndex>(anchorsDer.size()), &kCFTypeArrayCallBacks));
            for (const eastl::vector<uint8_t>& der : anchorsDer) {
                CfOwned<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, der.data(), static_cast<CFIndex>(der.size())));
                CfOwned<SecCertificateRef> certificate(data ? SecCertificateCreateWithData(kCFAllocatorDefault, data.get()) : nullptr);
                if (!certificate) {
                    return nullptr;
                }
                CFArrayAppendValue(array.get(), certificate.get());
            }
            return CfOwned<CFArrayRef>(static_cast<CFArrayRef>(array.release()));
        }

        /// The TLS stream of macOS over Network.framework (see Tls/CLAUDE.md for why not Secure Transport)
        class NetworkFrameworkTlsStream final : public ITlsStream {
        public:
            NetworkFrameworkTlsStream() : _state(std::make_shared<StreamState>()) {}

            ~NetworkFrameworkTlsStream() override {
                Close();
            }

            std::expected<void, eastl::string> Connect(const TlsConnectParams& params) override {
                const std::shared_ptr<StreamState> state = _state;
                const eastl::string host(params.host.data(), params.host.size());
                {
                    std::lock_guard lock(state->mutex);
                    state->target = Format("{}:{}", host, params.port);
                    if (state->phase != Phase::Idle) {
                        return std::unexpected(Format("TLS: stream already used for {}", state->target));
                    }
                    if (state->closing) {
                        return std::unexpected(Format("TLS: connect to {} cancelled", state->target));
                    }
                    state->phase = Phase::Connecting;
                }
                if (params.cancel != nullptr && params.cancel->load()) {
                    return Fail(*state, Format("connect to {} cancelled", state->target));
                }

                if (!params.additionalTrustAnchorsDer.empty()) {
                    CfOwned<CFArrayRef> anchors = MakeAnchors(params.additionalTrustAnchorsDer);
                    if (!anchors) {
                        return Fail(*state, Format("an additional trust anchor is not a certificate, not connecting to {}", state->target));
                    }
                    std::lock_guard lock(state->mutex);
                    state->anchors = std::move(anchors);
                }

                OsOwned<dispatch_queue_t> queueOwner(dispatch_queue_create("BagiTlsStream", DISPATCH_QUEUE_SERIAL));
                dispatch_queue_t queue = queueOwner.get();  // blocks copy what they capture; the owner stays here
                const bool hasAnchors = !params.additionalTrustAnchorsDer.empty();
                const uint32_t connectSeconds =
                    std::max<uint32_t>(kMinimumConnectTimeoutSeconds, static_cast<uint32_t>((params.connectTimeout + kMillisecondsPerSecond - std::chrono::milliseconds(1)) / kMillisecondsPerSecond));

                nw_parameters_configure_protocol_block_t configureTls = ^(nw_protocol_options_t tlsOptions) {
                  OsOwned<sec_protocol_options_t> secOptions(nw_tls_copy_sec_protocol_options(tlsOptions));
                  sec_protocol_options_set_min_tls_protocol_version(secOptions.get(), tls_protocol_version_TLSv12);
                  if (hasAnchors) {
                      // The only place the check is ever customised, and it is stricter or equal: the system's own evaluation, with
                      // our extra roots added. A block that returned true without evaluating would be a hole
                      sec_protocol_options_set_verify_block(
                          secOptions.get(),
                          ^(sec_protocol_metadata_t, sec_trust_t trust, sec_protocol_verify_complete_t complete) {
                            complete(EvaluateTrust(*state, host, trust));
                          },
                          queue);
                  }
                };
                nw_parameters_configure_protocol_block_t configureTcp = ^(nw_protocol_options_t tcpOptions) {
                  nw_tcp_options_set_no_delay(tcpOptions, true);
                  nw_tcp_options_set_connection_timeout(tcpOptions, connectSeconds);
                };
                OsOwned<nw_parameters_t> parameters(nw_parameters_create_secure_tcp(configureTls, configureTcp));
                OsOwned<nw_endpoint_t> endpoint(nw_endpoint_create_host(host.c_str(), Format("{}", params.port).c_str()));
                nw_connection_t connection = nw_connection_create(endpoint.get(), parameters.get());
                if (connection == nullptr) {
                    return Fail(*state, Format("cannot create a connection to {}", state->target));
                }
                {
                    std::lock_guard lock(state->mutex);
                    if (state->closing) {
                        nw_release(connection);
                        return std::unexpected(Format("TLS: connect to {} cancelled", state->target));
                    }
                    state->connection = connection;
                }
                nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t newState, nw_error_t error) {
                  state->OnState(newState, error);
                });
                nw_connection_set_queue(connection, queue);
                nw_connection_start(connection);

                return WaitUntilReady(*state, params);
            }

            bool SendAll(eastl::span<const uint8_t> data) override {
                if (data.empty()) {
                    return IsValid();
                }
                const std::shared_ptr<StreamState> state = _state;
                std::unique_lock lock(state->mutex);
                if (state->closing || state->phase != Phase::Ready || state->connection == nullptr) {
                    return false;
                }
                OsOwned<dispatch_data_t> content(dispatch_data_create(data.data(), data.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT));  // copies
                const uint64_t ticket = ++state->sendsIssued;
                nw_connection_send(state->connection, content.get(), NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t error) {
                  state->OnSent(error);
                });
                state->changed.wait(lock, [&] { return state->sendsCompleted >= ticket || state->closing || state->phase != Phase::Ready; });
                return state->sendsCompleted >= ticket && !state->sendFailed && !state->closing && state->phase == Phase::Ready;
            }

            std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer) override {
                if (buffer.empty()) {
                    return std::unexpected(SocketError::Failed);
                }
                const std::shared_ptr<StreamState> state = _state;
                std::unique_lock lock(state->mutex);
                const bool hasDeadline = state->receiveTimeout > std::chrono::milliseconds::zero();
                const auto deadline = hasDeadline ? std::chrono::steady_clock::now() + state->receiveTimeout : std::chrono::steady_clock::time_point::max();
                bool expired = false;
                for (;;) {
                    if (state->closing) {
                        return std::unexpected(SocketError::Closed);
                    }
                    if (state->phase == Phase::Idle || state->phase == Phase::Connecting) {
                        return std::unexpected(SocketError::Failed);
                    }
                    const size_t available = state->inbox.size() - state->inboxRead;
                    if (available > 0) {
                        const size_t count = std::min(available, buffer.size());
                        std::memcpy(buffer.data(), state->inbox.data() + state->inboxRead, count);
                        state->inboxRead += count;
                        if (state->inboxRead == state->inbox.size()) {
                            state->inbox.clear();  // keeps the capacity
                            state->inboxRead = 0;
                        }
                        return count;
                    }
                    if (state->endOfStream) {
                        return std::unexpected(SocketError::Closed);
                    }
                    if (state->phase != Phase::Ready) {
                        return std::unexpected(SocketError::Failed);
                    }
                    if (expired) {
                        return std::unexpected(SocketError::TimedOut);
                    }
                    state->StartReceiveLocked(state);  // one receive stays pending across a timeout; its data waits in the inbox
                    if (hasDeadline) {
                        expired = state->changed.wait_until(lock, deadline) == std::cv_status::timeout;
                    } else {
                        state->changed.wait(lock);
                    }
                }
            }

            bool SetReceiveTimeout(std::chrono::milliseconds timeout) override {
                std::lock_guard lock(_state->mutex);
                _state->receiveTimeout = timeout;
                return _state->phase == Phase::Ready;
            }

            void ShutdownBoth() override {
                std::lock_guard lock(_state->mutex);
                _state->closing = true;
                if (_state->connection != nullptr) {
                    nw_connection_cancel(_state->connection);
                }
                _state->changed.notify_all();
            }

            void Close() override {
                std::lock_guard lock(_state->mutex);
                _state->closing = true;
                _state->ReleaseConnectionLocked();
                _state->changed.notify_all();
            }

            bool IsValid() const override {
                std::lock_guard lock(_state->mutex);
                return _state->phase == Phase::Ready && !_state->closing;
            }

            eastl::string GetLastError() const override {
                std::lock_guard lock(_state->mutex);
                return _state->failure;
            }

        private:
            /// Records @p what as the failure (if none is recorded yet), drops the connection and returns it
            static std::unexpected<eastl::string> Fail(StreamState& state, const eastl::string& what) {
                std::lock_guard lock(state.mutex);
                if (state.phase != Phase::Failed) {
                    state.phase = Phase::Failed;
                    state.failure = Format("TLS: {}", what);
                }
                state.ReleaseConnectionLocked();
                return std::unexpected(state.failure);
            }

            /// Waits in slices of Socket::kWakeInterval for the state `ready` (TCP and TLS done) or a failure, the cancel flag, a Close() and the deadline
            static std::expected<void, eastl::string> WaitUntilReady(StreamState& state, const TlsConnectParams& params) {
                std::unique_lock lock(state.mutex);
                for (;;) {
                    if (state.phase == Phase::Ready) {
                        break;
                    }
                    if (state.phase == Phase::Failed || state.phase == Phase::Cancelled) {
                        state.ReleaseConnectionLocked();
                        if (state.failure.empty()) {
                            state.failure = Format("TLS: connect to {} cancelled", state.target);
                        }
                        return std::unexpected(state.failure);
                    }
                    const auto now = std::chrono::steady_clock::now();
                    const bool cancelled = state.closing || (params.cancel != nullptr && params.cancel->load());
                    const bool timedOut = now >= params.handshakeDeadline;
                    if (cancelled || timedOut) {
                        state.phase = Phase::Failed;
                        state.failure = timedOut && !cancelled ? Format("TLS: no handshake with {} in time", state.target) : Format("TLS: connect to {} cancelled", state.target);
                        state.ReleaseConnectionLocked();
                        return std::unexpected(state.failure);
                    }
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(params.handshakeDeadline - now) + std::chrono::milliseconds(1);
                    state.changed.wait_for(lock, std::min(Socket::kWakeInterval, left));
                }
                LogNegotiatedVersion(state);
                return {};
            }

            static void LogNegotiatedVersion(const StreamState& state) {
                OsOwned<nw_protocol_definition_t> tlsDefinition(nw_protocol_copy_tls_definition());
                OsOwned<nw_protocol_metadata_t> metadata(state.connection != nullptr ? nw_connection_copy_protocol_metadata(state.connection, tlsDefinition.get()) : nullptr);
                if (!metadata || !nw_protocol_metadata_is_tls(metadata.get())) {
                    return;
                }
                OsOwned<sec_protocol_metadata_t> secMetadata(nw_tls_copy_sec_protocol_metadata(metadata.get()));
                if (secMetadata) {
                    LOG_INFO("TLS to {}: TLS {}"_format(state.target, TlsVersionName(sec_protocol_metadata_get_negotiated_tls_protocol_version(secMetadata.get()))));
                }
            }

            std::shared_ptr<StreamState> _state;
        };

    }  // namespace

    eastl::string_view DescribeNetworkFailure(NetworkErrorDomain domain, int code) {
        switch (domain) {
            case NetworkErrorDomain::Posix:
                switch (code) {
                    case ECONNREFUSED:
                        return "connection refused";
                    case ETIMEDOUT:
                        return "timed out";
                    case ECONNRESET:
                    case ECONNABORTED:
                    case EPIPE:
                        return "connection reset";
                    case ENETUNREACH:
                    case EHOSTUNREACH:
                    case ENETDOWN:
                    case EHOSTDOWN:
                        return "host unreachable";
                    case ECANCELED:
                        return "cancelled";
                    default:
                        return "network error";
                }
            case NetworkErrorDomain::Dns:
                return "cannot resolve the host name";
            case NetworkErrorDomain::Tls:
            case NetworkErrorDomain::Other:
                break;
        }
        switch (code) {
            case errSSLXCertChainInvalid:  // the system's own check reports a name mismatch with this code too
                return "certificate is not trusted or does not match the host name";
            case errSSLUnknownRootCert:
            case errSSLNoRootCert:
            case errSSLBadCert:
            case errSSLPeerBadCert:
            case errSSLPeerUnknownCA:
            case errSecNotTrusted:
                return "certificate is not trusted";
            case errSSLHostNameMismatch:
            case errSecHostNameMismatch:
                return "host name does not match the certificate";
            case errSSLCertExpired:
            case errSSLPeerCertExpired:
            case errSecCertificateExpired:
                return "certificate has expired";
            case errSSLCertNotYetValid:
            case errSecCertificateNotValidYet:
                return "certificate is not valid yet";
            case errSSLClosedAbort:
            case errSSLClosedGraceful:
                return "the server closed the connection during the handshake";
            default:
                return "handshake failed";
        }
    }

    bool IsTlsSupported() {
        return true;
    }

    eastl::unique_ptr<ITlsStream> MakeTlsStream() {
        return eastl::make_unique<NetworkFrameworkTlsStream>();
    }

}  // namespace BECore

#endif  // PLATFORM_MACOS
