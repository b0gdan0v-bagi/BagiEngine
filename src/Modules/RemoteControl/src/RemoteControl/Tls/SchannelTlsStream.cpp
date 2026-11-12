#include "TlsStream.h"

// Written without a Windows compiler (see Tls/CLAUDE.md, "Windows: Schannel"): it was read against Microsoft's Schannel client
// sample and the SSPI documentation, and has not been built or run.
#ifdef PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The order matters (SSPI and Schannel use the types of wincrypt.h; security.h wants SECURITY_WIN32 first), so the formatter keeps off it
// clang-format off
#include <windows.h>
#include <wincrypt.h>
#include <schannel.h>
#define SECURITY_WIN32
#include <security.h>
// clang-format on
#include <shared_mutex>

namespace BECore {

    namespace {

        constexpr size_t kReceiveChunk = 16 * 1024;
        constexpr size_t kMaxIncomingBytes = 256 * 1024;  // a TLS record is at most 16 KiB plus its framing; more unread than this is a broken peer

        /// What the handshake may take: the caller's single deadline and its cancel flag (a renegotiation has neither)
        struct HandshakeLimits {
            std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
            const std::atomic<bool>* cancel = nullptr;
        };

        enum class Phase : uint8_t { Idle, Connecting, Ready, Failed };

        enum class DecryptResult : uint8_t { Plaintext, NeedMore, EndOfStream, Failed };

        /// The flags of every InitializeSecurityContext call of this stream. ISC_REQ_MANUAL_CRED_VALIDATION is added only on the
        /// extra-anchors path (a test): the default path lets Schannel check the chain and the name itself
        constexpr ULONG kContextRequests = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

        struct ContextBufferFreer {
            void operator()(void* buffer) const {
                if (buffer != nullptr) {
                    FreeContextBuffer(buffer);
                }
            }
        };

        using ContextBufferOwned = std::unique_ptr<void, ContextBufferFreer>;

        struct CertificateFreer {
            void operator()(const CERT_CONTEXT* certificate) const {
                if (certificate != nullptr) {
                    CertFreeCertificateContext(certificate);
                }
            }
        };

        struct StoreCloser {
            void operator()(void* store) const {
                if (store != nullptr) {
                    CertCloseStore(store, 0);
                }
            }
        };

        struct ChainEngineFreer {
            void operator()(void* engine) const {
                if (engine != nullptr) {
                    CertFreeCertificateChainEngine(engine);
                }
            }
        };

        struct ChainFreer {
            void operator()(const CERT_CHAIN_CONTEXT* chain) const {
                if (chain != nullptr) {
                    CertFreeCertificateChain(chain);
                }
            }
        };

        using CertificateOwned = std::unique_ptr<const CERT_CONTEXT, CertificateFreer>;
        using StoreOwned = std::unique_ptr<void, StoreCloser>;
        using ChainEngineOwned = std::unique_ptr<void, ChainEngineFreer>;
        using ChainOwned = std::unique_ptr<const CERT_CHAIN_CONTEXT, ChainFreer>;

        /// The words for the person for an SSPI status or a certificate chain policy error; the number goes only to the log.
        /// Pure (mirrors DescribeNetworkFailure of the macOS backend). The result is a string literal
        eastl::string_view DescribeSecurityFailure(long status) {
            switch (status) {
                case SEC_E_UNTRUSTED_ROOT:
                case CERT_E_UNTRUSTEDROOT:
                case CERT_E_CHAINING:
                case SEC_E_CERT_UNKNOWN:
                    return "certificate is not trusted";
                case SEC_E_WRONG_PRINCIPAL:
                case CERT_E_CN_NO_MATCH:
                    return "host name does not match the certificate";
                case SEC_E_CERT_EXPIRED:
                case CERT_E_EXPIRED:
                    return "certificate has expired or is not valid yet";
                case CERT_E_REVOKED:
                case CRYPT_E_REVOKED:
                    return "certificate has been revoked";
                case CERT_E_WRONG_USAGE:
                    return "certificate is not meant for a server";
                case SEC_E_ALGORITHM_MISMATCH:
                case SEC_E_UNSUPPORTED_FUNCTION:
                    return "no TLS version or cipher in common with the server";
                case SEC_E_ILLEGAL_MESSAGE:
                case SEC_E_MESSAGE_ALTERED:
                case SEC_E_DECRYPT_FAILURE:
                    return "the server sent data that is not valid TLS";
                case SEC_E_NO_CREDENTIALS:
                    return "no TLS credentials";
                default:
                    return "handshake failed";
            }
        }

        /// The buffer of @p type among @p buffers, or null
        const SecBuffer* FindBuffer(const SecBuffer* buffers, size_t count, ULONG type) {
            for (size_t index = 0; index < count; ++index) {
                if (buffers[index].BufferType == type) {
                    return &buffers[index];
                }
            }
            return nullptr;
        }

        /// SECBUFFER_EXTRA carry-over: the last @p tailBytes bytes of @p buffer are bytes of the next message that SSPI did not use; they
        /// become the whole buffer (zero drops everything)
        void KeepTail(eastl::vector<uint8_t>& buffer, size_t tailBytes) {
            if (tailBytes >= buffer.size()) {
                return;
            }
            if (tailBytes > 0) {
                std::memmove(buffer.data(), buffer.data() + buffer.size() - tailBytes, tailBytes);
            }
            buffer.resize(tailBytes);
        }

        /// @p extra (a SECBUFFER_EXTRA after a call, may be null) tells how much of the end of @p buffer was not consumed
        void KeepExtra(eastl::vector<uint8_t>& buffer, const SecBuffer* extra) {
            KeepTail(buffer, extra != nullptr ? static_cast<size_t>(extra->cbBuffer) : 0);
        }

        eastl::vector<wchar_t> ToWide(const eastl::string& text) {
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            eastl::vector<wchar_t> wide(static_cast<size_t>(std::max(length, 1)), L'\0');
            if (length > 0) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
            }
            return wide;
        }

        eastl::string_view TlsVersionName(DWORD protocol) {
#ifdef SP_PROT_TLS1_3_CLIENT
            if ((protocol & SP_PROT_TLS1_3_CLIENT) != 0) {
                return "1.3";
            }
#endif
            if ((protocol & SP_PROT_TLS1_2_CLIENT) != 0) {
                return "1.2";
            }
            return "of an unexpected version";
        }

        /// The TLS stream of Windows over Schannel (SSPI) on a Socket; see Tls/CLAUDE.md. Structure as the macOS backend: the same
        /// phases, the same texts. Not built or run yet
        class SchannelTlsStream final : public ITlsStream {
        public:
            ~SchannelTlsStream() override {
                Close();
            }

            std::expected<void, eastl::string> Connect(const TlsConnectParams& params) override {
                Phase expected = Phase::Idle;
                if (!_phase.compare_exchange_strong(expected, Phase::Connecting)) {
                    return std::unexpected(Format("TLS: stream already used for {}:{}", params.host, params.port));
                }
                _host.assign(params.host.data(), params.host.size());
                _target = Format("{}:{}", _host, params.port);
                _anchors.assign(params.additionalTrustAnchorsDer.begin(), params.additionalTrustAnchorsDer.end());
                if (_closing.load() || (params.cancel != nullptr && params.cancel->load())) {
                    return FailConnect(Format("connect to {} cancelled", _target));
                }

                // The TCP connection is made without any lock, so a Close() from another thread does not wait for it: it only sets
                // _closing and Socket::Connect() ends on the caller's cancel flag or its timeout; the socket is dropped below then
                auto connected = Socket::Connect(_host, params.port, params.connectTimeout, params.cancel);
                if (!connected) {
                    return FailConnect(connected.error());
                }
                {
                    std::unique_lock publish(_lifetime);
                    if (_closing.load()) {
                        return FailConnect(Format("connect to {} cancelled", _target));
                    }
                    _socket = std::move(*connected);
                }

                std::shared_lock life(_lifetime);  // Close() waits for the handshake; it is woken within Socket::kWakeInterval
                if (_closing.load()) {
                    return FailConnect(Format("connect to {} cancelled", _target));
                }
                const bool manualValidation = !_anchors.empty();
                if (auto credentials = AcquireCredentials(manualValidation); !credentials) {
                    return FailConnect(credentials.error());
                }
                HandshakeLimits limits;
                limits.deadline = params.handshakeDeadline;
                limits.cancel = params.cancel;
                if (auto handshake = RunHandshake(limits, false); !handshake) {
                    return FailConnect(handshake.error());
                }
                if (QueryContextAttributesA(&_context, SECPKG_ATTR_STREAM_SIZES, &_sizes) != SEC_E_OK) {
                    return FailConnect(Format("the stream sizes of {} are not known", _target));
                }
                LogNegotiatedVersion();
                _phase.store(Phase::Ready);
                return {};
            }

            bool SendAll(eastl::span<const uint8_t> data) override {
                if (data.empty()) {
                    return IsValid();
                }
                std::shared_lock life(_lifetime);
                if (_closing.load() || _phase.load() != Phase::Ready) {
                    return false;
                }
                // The records leave in the order they were encrypted (the TLS sequence numbers say so): the send lock covers encrypt and send.
                // Order of locks everywhere: send, then SSPI, never held while waiting for the socket by the SSPI one
                std::lock_guard sendOrder(_sendMutex);
                eastl::vector<uint8_t> record;
                size_t offset = 0;
                while (offset < data.size()) {
                    if (_closing.load()) {
                        return false;
                    }
                    const size_t chunk = std::min<size_t>(data.size() - offset, _sizes.cbMaximumMessage);
                    record.resize(static_cast<size_t>(_sizes.cbHeader) + chunk + _sizes.cbTrailer);
                    std::memcpy(record.data() + _sizes.cbHeader, data.data() + offset, chunk);

                    // EncryptMessage(): header, data and trailer buffers lie one after the other in @c record; on return the three hold the
                    // record (their sizes may have shrunk), the fourth is unused
                    SecBuffer buffers[4] = {};
                    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
                    buffers[0].pvBuffer = record.data();
                    buffers[0].cbBuffer = _sizes.cbHeader;
                    buffers[1].BufferType = SECBUFFER_DATA;
                    buffers[1].pvBuffer = record.data() + _sizes.cbHeader;
                    buffers[1].cbBuffer = static_cast<ULONG>(chunk);
                    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
                    buffers[2].pvBuffer = record.data() + _sizes.cbHeader + chunk;
                    buffers[2].cbBuffer = _sizes.cbTrailer;
                    buffers[3].BufferType = SECBUFFER_EMPTY;
                    SecBufferDesc description{SECBUFFER_VERSION, 4, buffers};
                    SECURITY_STATUS status = SEC_E_OK;
                    {
                        std::lock_guard sspi(_sspiMutex);
                        status = EncryptMessage(&_context, 0, &description, 0);
                    }
                    if (status != SEC_E_OK) {
                        SetFailure(Format("TLS: {}: {}", DescribeSecurityFailure(status), _target), status);
                        return false;
                    }
                    size_t total = 0;
                    for (int index = 0; index < 3; ++index) {
                        std::memmove(record.data() + total, buffers[index].pvBuffer, buffers[index].cbBuffer);
                        total += buffers[index].cbBuffer;
                    }
                    if (!_socket.SendAll(eastl::span<const uint8_t>(record.data(), total))) {
                        if (!_closing.load()) {
                            SetFailure(Format("TLS: connection lost while sending to {}", _target), 0);
                        }
                        return false;
                    }
                    offset += chunk;
                }
                return true;
            }

            std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer) override {
                if (buffer.empty()) {
                    return std::unexpected(SocketError::Failed);
                }
                std::shared_lock life(_lifetime);
                uint8_t chunk[kReceiveChunk];
                for (;;) {
                    if (_closing.load()) {
                        return std::unexpected(SocketError::Closed);
                    }
                    const Phase phase = _phase.load();
                    if (phase != Phase::Ready) {
                        return std::unexpected(SocketError::Failed);
                    }
                    if (_plainRead < _plain.size()) {
                        const size_t count = std::min(_plain.size() - _plainRead, buffer.size());
                        std::memcpy(buffer.data(), _plain.data() + _plainRead, count);
                        _plainRead += count;
                        if (_plainRead == _plain.size()) {
                            _plain.clear();  // keeps the capacity
                            _plainRead = 0;
                        }
                        return count;
                    }
                    switch (DecryptPending()) {
                        case DecryptResult::Plaintext:
                            continue;
                        case DecryptResult::EndOfStream:
                            return std::unexpected(SocketError::Closed);
                        case DecryptResult::Failed:
                            return std::unexpected(SocketError::Failed);
                        case DecryptResult::NeedMore:
                            break;
                    }
                    // A partial record stays in _incoming across a receive timeout; the next call goes on with it
                    const auto received = _socket.Receive(eastl::span<uint8_t>(chunk, kReceiveChunk));
                    if (!received) {
                        if (received.error() == SocketError::Failed && !_closing.load()) {
                            SetFailure(Format("TLS: connection reset by {}", _target), 0);
                        }
                        return std::unexpected(received.error());
                    }
                    if (_incoming.size() + *received > kMaxIncomingBytes) {
                        SetFailure(Format("TLS: {} sent a record that is too large", _target), 0);
                        return std::unexpected(SocketError::Failed);
                    }
                    _incoming.insert(_incoming.end(), chunk, chunk + *received);
                }
            }

            bool SetReceiveTimeout(std::chrono::milliseconds timeout) override {
                std::shared_lock life(_lifetime);
                _receiveTimeoutMs.store(timeout.count());
                return _phase.load() == Phase::Ready && _socket.SetReceiveTimeout(timeout);
            }

            void ShutdownBoth() override {
                _closing.store(true);
                std::shared_lock life(_lifetime);
                _socket.ShutdownBoth();  // wakes the Receive() and SendAll() that wait on the socket, within Socket::kWakeInterval
            }

            void Close() override {
                if (_phase.load() == Phase::Ready && !_closing.load()) {
                    std::shared_lock life(_lifetime);
                    SendCloseNotify();
                }
                ShutdownBoth();
                // Nobody is inside a call any more once this lock is ours: the woken ones leave within Socket::kWakeInterval
                std::unique_lock exclusive(_lifetime);
                if (_hasContext) {
                    DeleteSecurityContext(&_context);
                    _hasContext = false;
                }
                if (_hasCredentials) {
                    FreeCredentialsHandle(&_credentials);
                    _hasCredentials = false;
                }
                _socket.Close();
            }

            bool IsValid() const override {
                return _phase.load() == Phase::Ready && !_closing.load();
            }

            eastl::string GetLastError() const override {
                std::lock_guard lock(_errorMutex);
                return _failure;
            }

        private:
            /// Records @p what as the failure (the first one wins) and returns it as the error of Connect()
            std::unexpected<eastl::string> FailConnect(const eastl::string& what) {
                SetFailure(what.find("TLS") == 0 ? what : Format("TLS: {}", what), 0);
                return std::unexpected(GetLastError());
            }

            void SetFailure(const eastl::string& text, SECURITY_STATUS status) {
                {
                    std::lock_guard lock(_errorMutex);
                    if (_failure.empty()) {
                        _failure = text;
                        LOG_WARNING("{} (status 0x{:08X})"_format(_failure, static_cast<uint32_t>(status)));
                    }
                }
                _phase.store(Phase::Failed);
            }

            /// AcquireCredentialsHandle(): the client credentials of Schannel. With the legacy SCHANNEL_CRED Windows 11 would not offer TLS 1.3,
            /// so SCH_CREDENTIALS is tried first (where the SDK has it) and the legacy structure is the fallback of an older Windows.
            /// Without @p manualValidation Schannel validates the chain and the host name itself (the target name passed to
            /// InitializeSecurityContext); no flag turns that off
            std::expected<void, eastl::string> AcquireCredentials(bool manualValidation) {
                const DWORD flags = SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_DEFAULT_CREDS | (manualValidation ? SCH_CRED_MANUAL_CRED_VALIDATION : SCH_CRED_AUTO_CRED_VALIDATION);
                TimeStamp expiry{};
                SECURITY_STATUS status = SEC_E_INTERNAL_ERROR;
#ifdef SCH_CREDENTIALS_VERSION
                SCH_CREDENTIALS credentials{};
                credentials.dwVersion = SCH_CREDENTIALS_VERSION;
                credentials.dwFlags = flags;
                status = AcquireCredentialsHandleA(nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr, &credentials, nullptr, nullptr, &_credentials, &expiry);
#endif
                if (status != SEC_E_OK) {
                    SCHANNEL_CRED legacy{};
                    legacy.dwVersion = SCHANNEL_CRED_VERSION;
                    legacy.dwFlags = flags;
                    status = AcquireCredentialsHandleA(nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr, &legacy, nullptr, nullptr, &_credentials, &expiry);
                }
                if (status != SEC_E_OK) {
                    LOG_WARNING("TLS: AcquireCredentialsHandle failed (status 0x{:08X})"_format(static_cast<uint32_t>(status)));
                    return std::unexpected(Format("TLS: no TLS credentials: {}", _target));
                }
                _hasCredentials = true;
                return {};
            }

            /// The InitializeSecurityContext loop of Microsoft's Schannel client sample. @p resumed: the context exists and _incoming holds
            /// the handshake message of a renegotiation (DecryptMessage() returned SEC_I_RENEGOTIATE); otherwise this is the first flight.
            /// Ends with SEC_E_OK (the handshake is done; in manual mode the chain is then checked by hand), a failure text, a cancel or the deadline
            std::expected<void, eastl::string> RunHandshake(const HandshakeLimits& limits, bool resumed) {
                const bool manualValidation = !_anchors.empty();
                _socket.SetReceiveTimeout(Socket::kWakeInterval);  // the reads below look at the cancel flag and the deadline between slices
                const std::expected<void, eastl::string> outcome = HandshakeLoop(limits, resumed, manualValidation);
                _socket.SetReceiveTimeout(std::chrono::milliseconds(_receiveTimeoutMs.load()));
                if (!outcome) {
                    return outcome;
                }
                if (manualValidation) {
                    return ValidateServerCertificate();
                }
                return {};
            }

            std::expected<void, eastl::string> HandshakeLoop(const HandshakeLimits& limits, bool resumed, bool manualValidation) {
                const ULONG requests = kContextRequests | (manualValidation ? ISC_REQ_MANUAL_CRED_VALIDATION : 0);
                bool first = !resumed;
                bool needData = false;  // the first flight has nothing to read yet (the answer to it does); a renegotiation starts with its message
                for (;;) {
                    if (needData) {
                        if (auto more = ReadCiphertext(limits); !more) {
                            return more;
                        }
                        needData = false;
                    }

                    // The input is everything received so far that SSPI has not consumed; after the call SECBUFFER_EXTRA tells how much of its end
                    // belongs to the next message
                    SecBuffer inBuffers[2] = {};
                    inBuffers[0].BufferType = SECBUFFER_TOKEN;
                    inBuffers[0].pvBuffer = _incoming.data();
                    inBuffers[0].cbBuffer = static_cast<ULONG>(_incoming.size());
                    inBuffers[1].BufferType = SECBUFFER_EMPTY;
                    SecBufferDesc inDescription{SECBUFFER_VERSION, 2, inBuffers};
                    SecBuffer outBuffer = {};
                    outBuffer.BufferType = SECBUFFER_TOKEN;  // ISC_REQ_ALLOCATE_MEMORY: SSPI allocates the token, we FreeContextBuffer() it
                    SecBufferDesc outDescription{SECBUFFER_VERSION, 1, &outBuffer};
                    ULONG attributes = 0;
                    TimeStamp expiry{};
                    SECURITY_STATUS status = SEC_E_OK;
                    {
                        std::lock_guard sspi(_sspiMutex);
                        status = InitializeSecurityContextA(&_credentials, first ? nullptr : &_context, const_cast<SEC_CHAR*>(_host.c_str()), requests, 0, 0, first ? nullptr : &inDescription, 0,
                                                            &_context, &outDescription, &attributes, &expiry);
                    }
                    if (status >= 0) {
                        _hasContext = true;
                    }
                    first = false;
                    ContextBufferOwned token(outBuffer.pvBuffer);
                    // A token goes out even with a failure status (it may be an alert); its own failure matters only when the status was fine
                    bool sent = true;
                    if (outBuffer.cbBuffer > 0 && outBuffer.pvBuffer != nullptr) {
                        sent = SendRaw(eastl::span<const uint8_t>(static_cast<const uint8_t*>(outBuffer.pvBuffer), outBuffer.cbBuffer));
                    }

                    if (status == SEC_E_OK) {
                        KeepExtra(_incoming, FindBuffer(inBuffers, 2, SECBUFFER_EXTRA));  // application data that came with the last flight
                        return sent ? std::expected<void, eastl::string>() : std::unexpected(Format("TLS: connection lost during the handshake with {}", _target));
                    }
                    if (status == SEC_I_CONTINUE_NEEDED) {
                        if (!sent) {
                            return std::unexpected(Format("TLS: connection lost during the handshake with {}", _target));
                        }
                        KeepExtra(_incoming, FindBuffer(inBuffers, 2, SECBUFFER_EXTRA));
                        needData = _incoming.empty();  // with bytes left over the next call needs no read
                        continue;
                    }
                    if (status == SEC_E_INCOMPLETE_MESSAGE) {
                        needData = true;  // nothing was consumed; the same bytes plus more go in again
                        continue;
                    }
                    if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
                        return std::unexpected(Format("TLS: {} asks for a client certificate, which this client does not have", _target));
                    }
                    if (status == SEC_I_CONTEXT_EXPIRED) {
                        return std::unexpected(Format("TLS: the server closed the connection during the handshake: {}", _target));
                    }
                    LOG_WARNING("TLS: handshake with {} failed (status 0x{:08X})"_format(_target, static_cast<uint32_t>(status)));
                    return std::unexpected(Format("TLS: {}: {}", DescribeSecurityFailure(status), _target));
                }
            }

            /// Appends what the socket gives to _incoming, in slices of Socket::kWakeInterval that look at the cancel flag, Close() and the deadline
            std::expected<void, eastl::string> ReadCiphertext(const HandshakeLimits& limits) {
                uint8_t chunk[kReceiveChunk];
                for (;;) {
                    if (_closing.load() || (limits.cancel != nullptr && limits.cancel->load())) {
                        return std::unexpected(Format("TLS: connect to {} cancelled", _target));
                    }
                    if (std::chrono::steady_clock::now() >= limits.deadline) {
                        return std::unexpected(Format("TLS: no handshake with {} in time", _target));
                    }
                    const auto received = _socket.Receive(eastl::span<uint8_t>(chunk, kReceiveChunk));
                    if (received) {
                        if (_incoming.size() + *received > kMaxIncomingBytes) {
                            return std::unexpected(Format("TLS: {} sent a record that is too large", _target));
                        }
                        _incoming.insert(_incoming.end(), chunk, chunk + *received);
                        return {};
                    }
                    if (received.error() == SocketError::TimedOut) {
                        continue;
                    }
                    if (received.error() == SocketError::Closed) {
                        return std::unexpected(_closing.load() ? Format("TLS: connect to {} cancelled", _target) : Format("TLS: the server closed the connection during the handshake: {}", _target));
                    }
                    return std::unexpected(Format("TLS: connection reset during the handshake with {}", _target));
                }
            }

            /// Sends bytes that are not application data (a handshake token, close_notify), in order with the encrypted records
            bool SendRaw(eastl::span<const uint8_t> bytes) {
                std::lock_guard sendOrder(_sendMutex);
                return _socket.SendAll(bytes);
            }

            /// DecryptMessage() on what _incoming holds. Plaintext: _plain got bytes. NeedMore: only part of a record is there. Called by the receiving thread
            DecryptResult DecryptPending() {
                for (;;) {
                    if (_incoming.empty()) {
                        return DecryptResult::NeedMore;
                    }
                    // In place: [0] the received bytes; after the call the decrypted data is a SECBUFFER_DATA inside it and the bytes of the next
                    // record(s) are a SECBUFFER_EXTRA at its end
                    SecBuffer buffers[4] = {};
                    buffers[0].BufferType = SECBUFFER_DATA;
                    buffers[0].pvBuffer = _incoming.data();
                    buffers[0].cbBuffer = static_cast<ULONG>(_incoming.size());
                    buffers[1].BufferType = SECBUFFER_EMPTY;
                    buffers[2].BufferType = SECBUFFER_EMPTY;
                    buffers[3].BufferType = SECBUFFER_EMPTY;
                    SecBufferDesc description{SECBUFFER_VERSION, 4, buffers};
                    SECURITY_STATUS status = SEC_E_OK;
                    {
                        std::lock_guard sspi(_sspiMutex);
                        status = DecryptMessage(&_context, &description, 0, nullptr);
                    }
                    const SecBuffer* extra = FindBuffer(buffers, 4, SECBUFFER_EXTRA);
                    if (status == SEC_E_OK) {
                        const SecBuffer* data = FindBuffer(buffers, 4, SECBUFFER_DATA);
                        const bool gotBytes = data != nullptr && data->cbBuffer > 0;
                        if (gotBytes) {
                            const auto* begin = static_cast<const uint8_t*>(data->pvBuffer);
                            _plain.insert(_plain.end(), begin, begin + data->cbBuffer);  // copied before _incoming is shortened below
                        }
                        KeepExtra(_incoming, extra);
                        if (gotBytes) {
                            return DecryptResult::Plaintext;
                        }
                        continue;  // a record without application data (a handshake message of TLS 1.3): the next one may follow at once
                    }
                    if (status == SEC_E_INCOMPLETE_MESSAGE) {
                        return DecryptResult::NeedMore;
                    }
                    if (status == SEC_I_CONTEXT_EXPIRED) {
                        return DecryptResult::EndOfStream;  // close_notify from the server
                    }
                    if (status == SEC_I_RENEGOTIATE) {
                        // The server wants a new handshake (TLS 1.2) or sent a post-handshake message of TLS 1.3: the extra bytes are its message, and
                        // go to InitializeSecurityContext as input
                        KeepExtra(_incoming, extra);
                        const HandshakeLimits unlimited;
                        if (auto renegotiated = RunHandshake(unlimited, true); !renegotiated) {
                            SetFailure(renegotiated.error(), status);
                            return DecryptResult::Failed;
                        }
                        continue;
                    }
                    if (!_closing.load()) {
                        SetFailure(Format("TLS: {}: {}", DescribeSecurityFailure(status), _target), status);
                    }
                    return DecryptResult::Failed;
                }
            }

            /// Best effort, once: tells the server we are done (ApplyControlToken with SCHANNEL_SHUTDOWN, then the token InitializeSecurityContext
            /// gives is the close_notify alert). Skipped when a sender holds the send lock; the caller holds the lifetime lock
            void SendCloseNotify() {
                std::unique_lock sendOrder(_sendMutex, std::try_to_lock);
                if (!sendOrder.owns_lock() || !_hasContext) {
                    return;
                }
                DWORD shutdownType = SCHANNEL_SHUTDOWN;
                SecBuffer control = {};
                control.BufferType = SECBUFFER_TOKEN;
                control.pvBuffer = &shutdownType;
                control.cbBuffer = sizeof(shutdownType);
                SecBufferDesc controlDescription{SECBUFFER_VERSION, 1, &control};
                SecBuffer outBuffer = {};
                outBuffer.BufferType = SECBUFFER_TOKEN;
                SecBufferDesc outDescription{SECBUFFER_VERSION, 1, &outBuffer};
                ULONG attributes = 0;
                TimeStamp expiry{};
                SECURITY_STATUS status = SEC_E_OK;
                {
                    std::lock_guard sspi(_sspiMutex);
                    if (ApplyControlToken(&_context, &controlDescription) != SEC_E_OK) {
                        return;
                    }
                    status = InitializeSecurityContextA(&_credentials, &_context, const_cast<SEC_CHAR*>(_host.c_str()), kContextRequests, 0, 0, nullptr, 0, &_context, &outDescription, &attributes,
                                                        &expiry);
                }
                ContextBufferOwned token(outBuffer.pvBuffer);
                if (status >= 0 && outBuffer.cbBuffer > 0 && outBuffer.pvBuffer != nullptr) {
                    (void)_socket.SendAll(eastl::span<const uint8_t>(static_cast<const uint8_t*>(outBuffer.pvBuffer), outBuffer.cbBuffer));
                }
            }

            /// The extra-anchors path (tests only): the chain and the host name checked by hand, after Schannel, which was told not to,
            /// finished the handshake. The roots are the system's "ROOT" store plus the anchors, and nothing else; CertVerifyCertificateChainPolicy with
            /// CERT_CHAIN_POLICY_SSL checks the chain, the validity, the server-auth usage and the host name. Revocation is not looked at here.
            /// THE PART MOST LIKELY TO NEED A FIX at the first run on Windows (the root store handling in particular)
            std::expected<void, eastl::string> ValidateServerCertificate() {
                PCCERT_CONTEXT remote = nullptr;
                if (QueryContextAttributesA(&_context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &remote) != SEC_E_OK || remote == nullptr) {
                    return std::unexpected(Format("TLS: the server sent no certificate: {}", _target));
                }
                const CertificateOwned serverCertificate(remote);

                StoreOwned roots(CertOpenSystemStoreW(0, L"ROOT"));
                StoreOwned anchorStore(CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr));
                StoreOwned collection(CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr));
                if (!roots || !anchorStore || !collection) {
                    return std::unexpected(Format("TLS: the certificate stores cannot be opened: {}", _target));
                }
                for (const eastl::vector<uint8_t>& der : _anchors) {
                    if (!CertAddEncodedCertificateToStore(anchorStore.get(), X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS, nullptr)) {
                        return std::unexpected(Format("TLS: an additional trust anchor is not a certificate: {}", _target));
                    }
                }
                if (!CertAddStoreToCollection(collection.get(), roots.get(), 0, 0) || !CertAddStoreToCollection(collection.get(), anchorStore.get(), 0, 0)) {
                    return std::unexpected(Format("TLS: the certificate stores cannot be combined: {}", _target));
                }

                CERT_CHAIN_ENGINE_CONFIG engineConfig = {};
                engineConfig.cbSize = sizeof(engineConfig);
                engineConfig.hExclusiveRoot = collection.get();  // only these are trusted roots
                HCERTCHAINENGINE rawEngine = nullptr;
                if (!CertCreateCertificateChainEngine(&engineConfig, &rawEngine)) {
                    return std::unexpected(Format("TLS: the certificate chain engine cannot be created: {}", _target));
                }
                const ChainEngineOwned engine(rawEngine);

                LPSTR serverAuthUsage = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
                CERT_CHAIN_PARA chainParameters = {};
                chainParameters.cbSize = sizeof(chainParameters);
                chainParameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
                chainParameters.RequestedUsage.Usage.cUsageIdentifier = 1;
                chainParameters.RequestedUsage.Usage.rgpszUsageIdentifier = &serverAuthUsage;
                PCCERT_CHAIN_CONTEXT rawChain = nullptr;
                if (!CertGetCertificateChain(engine.get(), serverCertificate.get(), nullptr, serverCertificate->hCertStore, &chainParameters, 0, nullptr, &rawChain)) {
                    return std::unexpected(Format("TLS: the certificate chain cannot be built: {}", _target));
                }
                const ChainOwned chain(rawChain);

                const eastl::vector<wchar_t> serverName = ToWide(_host);
                SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslParameters = {};
                sslParameters.cbSize = sizeof(sslParameters);
                sslParameters.dwAuthType = AUTHTYPE_SERVER;
                sslParameters.pwszServerName = const_cast<wchar_t*>(serverName.data());
                CERT_CHAIN_POLICY_PARA policyParameters = {};
                policyParameters.cbSize = sizeof(policyParameters);
                policyParameters.pvExtraPolicyPara = &sslParameters;
                CERT_CHAIN_POLICY_STATUS policyStatus = {};
                policyStatus.cbSize = sizeof(policyStatus);
                if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain.get(), &policyParameters, &policyStatus)) {
                    return std::unexpected(Format("TLS: the certificate of {} cannot be checked", _target));
                }
                if (policyStatus.dwError != 0) {
                    const auto policyError = static_cast<long>(policyStatus.dwError);
                    LOG_WARNING("TLS: certificate of {} refused (policy error 0x{:08X})"_format(_target, static_cast<uint32_t>(policyStatus.dwError)));
                    return std::unexpected(Format("TLS: {}: {}", DescribeSecurityFailure(policyError), _target));
                }
                return {};
            }

            void LogNegotiatedVersion() {
                SecPkgContext_ConnectionInfo info = {};
                if (QueryContextAttributesA(&_context, SECPKG_ATTR_CONNECTION_INFO, &info) == SEC_E_OK) {
                    LOG_INFO("TLS to {}: TLS {}"_format(_target, TlsVersionName(info.dwProtocol)));
                }
            }

            // Order of the locks: _lifetime, then _sendMutex, then _sspiMutex. The SSPI mutex is held only for the SSPI call itself, never
            // while waiting on the socket
            mutable std::shared_mutex _lifetime;  // shared by every call that uses the socket or the context, exclusive in Close()
            std::mutex _sspiMutex;                // a Schannel context is not safe for concurrent calls
            std::mutex _sendMutex;                // the ciphertext leaves in the order it was made
            mutable std::mutex _errorMutex;
            eastl::string _failure;

            std::atomic<Phase> _phase{Phase::Idle};
            std::atomic<bool> _closing{false};
            std::atomic<std::chrono::milliseconds::rep> _receiveTimeoutMs{0};

            eastl::string _host;
            eastl::string _target;  // host:port, for the texts
            TlsTrustAnchors _anchors;

            Socket _socket;
            CredHandle _credentials = {};
            CtxtHandle _context = {};
            bool _hasCredentials = false;
            bool _hasContext = false;
            SecPkgContext_StreamSizes _sizes = {};

            // Touched by the receiving thread only (and by Connect() before there is one)
            eastl::vector<uint8_t> _incoming;  // ciphertext received and not yet decrypted
            eastl::vector<uint8_t> _plain;     // decrypted and not yet handed out
            size_t _plainRead = 0;
        };

    }  // namespace

    bool IsTlsSupported() {
        return true;
    }

    eastl::unique_ptr<ITlsStream> MakeTlsStream() {
        return eastl::make_unique<SchannelTlsStream>();
    }

}  // namespace BECore

#endif  // PLATFORM_WINDOWS
