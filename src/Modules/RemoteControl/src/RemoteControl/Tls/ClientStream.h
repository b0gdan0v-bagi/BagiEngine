#pragma once

#include <RemoteControl/Tls/TlsStream.h>
#include <RemoteControl/WebSocketUrl.h>

namespace BECore {

    /**
     * @brief The byte stream of a WebSocketClient: a plain Socket (`ws://`) or an ITlsStream (`wss://`) behind one interface.
     *
     * Move-only. The calls after Connect() have Socket's contract (and ITlsStream's, which is the same): one thread in Receive(), one
     * in SendAll() and one in ShutdownBoth() at once.
     */
    class ClientStream {
    public:
        ClientStream() = default;
        ~ClientStream() = default;

        ClientStream(const ClientStream&) = delete;
        ClientStream& operator=(const ClientStream&) = delete;
        ClientStream(ClientStream&&) noexcept = default;
        ClientStream& operator=(ClientStream&&) noexcept = default;

        /// Connects to @p url's host and port: `ws` through Socket::Connect(), `wss` through the stream that @p tls.factory makes
        /// (MakeTlsStream() when it is empty) with the handshake. @p cancel is read from this thread and set from another: the
        /// connect or the handshake then ends within Socket::kWakeInterval. @p handshakeDeadline is only for TLS (the caller waits
        /// for the opening answer by its own clock); @p connectTimeout is the TCP connect's. The error names host:port
        [[nodiscard]] static std::expected<ClientStream, String> Connect(const WebSocketUrl& url, const TlsClientOptions& tls, std::chrono::milliseconds connectTimeout,
                                                                         std::chrono::steady_clock::time_point handshakeDeadline, const std::atomic<bool>* cancel);

        [[nodiscard]] bool SendAll(eastl::span<const uint8_t> data);
        [[nodiscard]] std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer);
        bool SetReceiveTimeout(std::chrono::milliseconds timeout);
        void ShutdownBoth();
        void Close();
        [[nodiscard]] bool IsValid() const;

        /// Why the last call that failed failed, when the stream can tell (TLS: the certificate, the name ...); "" for a plain socket
        [[nodiscard]] String GetLastError() const;

    private:
        Socket _socket;                      // `ws`
        eastl::unique_ptr<ITlsStream> _tls;  // `wss`: a connected stream, or null
    };

}  // namespace BECore
