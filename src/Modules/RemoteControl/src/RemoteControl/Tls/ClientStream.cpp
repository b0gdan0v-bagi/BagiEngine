#include "ClientStream.h"

namespace BECore {

    std::expected<ClientStream, String> ClientStream::Connect(const WebSocketUrl& url, const TlsClientOptions& tls, std::chrono::milliseconds connectTimeout,
                                                              std::chrono::steady_clock::time_point handshakeDeadline, const std::atomic<bool>* cancel) {
        ClientStream stream;
        if (!url.secure) {
            auto connected = Socket::Connect(url.host, url.port, connectTimeout, cancel);
            if (!connected) {
                return std::unexpected(std::move(connected.error()));
            }
            stream._socket = std::move(*connected);
            return stream;
        }

        stream._tls = tls.factory ? tls.factory() : MakeTlsStream();
        if (!stream._tls) {
            return std::unexpected(Format("TLS: no stream for {}:{}", url.host, url.port));
        }
        TlsConnectParams params;
        params.host = StringView(url.host);
        params.port = url.port;
        params.connectTimeout = connectTimeout;
        params.handshakeDeadline = handshakeDeadline;
        params.cancel = cancel;
        params.additionalTrustAnchorsDer = Span<const Vector<uint8_t>>(tls.additionalTrustAnchorsDer.data(), tls.additionalTrustAnchorsDer.size());
        const auto connected = stream._tls->Connect(params);
        if (!connected) {
            return std::unexpected(connected.error());
        }
        return stream;
    }

    bool ClientStream::SendAll(Span<const uint8_t> data) {
        return _tls ? _tls->SendAll(data) : _socket.SendAll(data);
    }

    std::expected<size_t, SocketError> ClientStream::Receive(Span<uint8_t> buffer) {
        return _tls ? _tls->Receive(buffer) : _socket.Receive(buffer);
    }

    bool ClientStream::SetReceiveTimeout(std::chrono::milliseconds timeout) {
        return _tls ? _tls->SetReceiveTimeout(timeout) : _socket.SetReceiveTimeout(timeout);
    }

    void ClientStream::ShutdownBoth() {
        if (_tls) {
            _tls->ShutdownBoth();
        } else {
            _socket.ShutdownBoth();
        }
    }

    void ClientStream::Close() {
        if (_tls) {
            _tls->Close();
            _tls.reset();
        }
        _socket.Close();
    }

    bool ClientStream::IsValid() const {
        return _tls ? _tls->IsValid() : _socket.IsValid();
    }

    String ClientStream::GetLastError() const {
        return _tls ? _tls->GetLastError() : String();
    }

}  // namespace BECore
