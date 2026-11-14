#pragma once

#include <RemoteControl/Socket.h>

namespace BECore {

    /// The error text of a TLS connect on a platform without a backend starts with this (and WebSocketClient::Open() refuses `wss://` with it)
    inline constexpr StringView kTlsUnsupported = "tls_unsupported";

    /// DER certificates (one X.509 certificate each) that are trusted in addition to the system's
    using TlsTrustAnchors = Vector<Vector<uint8_t>>;

    /// What ITlsStream::Connect() needs; the views only have to live until Connect() returns
    struct TlsConnectParams {
        /// The server's name: where to connect, and the name the certificate has to be valid for (SNI)
        StringView host;
        uint16_t port = 0;

        /// How long the TCP connect may take (the name lookup before it is not bounded)
        std::chrono::milliseconds connectTimeout{10000};

        /// When the TLS handshake has to be done at the latest. It is the deadline of the caller's whole opening, so the
        /// caller can spend what is left of it after Connect() on the next step
        std::chrono::steady_clock::time_point handshakeDeadline = std::chrono::steady_clock::time_point::max();

        /// Read from the connecting thread, set from another: Connect() ends with "cancelled" within Socket::kWakeInterval after it became true
        const std::atomic<bool>* cancel = nullptr;

        /// Trusted in addition to the system's. The chain, the validity and the host name are still checked; this is no way around the check
        Span<const Vector<uint8_t>> additionalTrustAnchorsDer;
    };

    /**
     * @brief A client TLS byte stream, shaped like Socket: one implementation per platform, chosen at build time (MakeTlsStream()).
     *
     * It owns the TCP connection (a system TLS library such as Network.framework cannot be layered on a file descriptor).
     * Connect() resolves, connects and does the handshake; the certificate chain is checked against the system trust store and
     * @c host always, and there is no switch that turns the check off.
     *
     * **Threads, as Socket's:** after Connect() has returned, one thread may be in Receive(), another in SendAll() and a third in
     * ShutdownBoth() or Close() at the same time. ShutdownBoth() wakes a Receive() or SendAll() that waits (on Windows within
     * Socket::kWakeInterval). Connect() happens before any of them and honours TlsConnectParams::cancel. Close() and the destructor
     * are safe in every state. The errors are SocketError (as Socket's) plus a text for the person, GetLastError(): why (the
     * certificate, the name, the handshake, a reset).
     */
    class ITlsStream {
    public:
        virtual ~ITlsStream() = default;

        /// Connects and does the handshake. The error is a message for the log and for the player and names host:port
        [[nodiscard]] virtual std::expected<void, String> Connect(const TlsConnectParams& params) = 0;

        /// Sends all of @p data; false when the stream is gone
        [[nodiscard]] virtual bool SendAll(Span<const uint8_t> data) = 0;

        /// Reads what has arrived, at most @p buffer.size() bytes (always at least one). Closed: the peer ended the stream or ShutdownBoth() was called
        [[nodiscard]] virtual std::expected<size_t, SocketError> Receive(Span<uint8_t> buffer) = 0;

        /// Receive() gives up with TimedOut after @p timeout; zero waits for ever
        virtual bool SetReceiveTimeout(std::chrono::milliseconds timeout) = 0;

        /// Ends the stream in both directions and wakes the threads blocked in Receive() or SendAll()
        virtual void ShutdownBoth() = 0;

        virtual void Close() = 0;

        /// True from a successful Connect() until Close()
        [[nodiscard]] virtual bool IsValid() const = 0;

        /// Why the last call that failed failed ("" before any failure); a copy, so any thread may ask
        [[nodiscard]] virtual String GetLastError() const = 0;
    };

    using TlsStreamFactory = eastl::function<UniquePtr<ITlsStream>()>;

    /// The TLS part of WebSocketClientSettings
    struct TlsClientOptions {
        /// See TlsConnectParams::additionalTrustAnchorsDer. It exists so a test can trust its own CA
        TlsTrustAnchors additionalTrustAnchorsDer;

        /// Makes the stream of a `wss://` connection; empty means MakeTlsStream() (a test puts a fake backend here)
        TlsStreamFactory factory;
    };

    /// True when this build has a TLS backend (macOS, Windows); the stub of the other platforms says false
    [[nodiscard]] bool IsTlsSupported();

    /// The stream of this platform's backend; on a platform without one a stream whose Connect() fails with kTlsUnsupported
    [[nodiscard]] UniquePtr<ITlsStream> MakeTlsStream();

}  // namespace BECore
