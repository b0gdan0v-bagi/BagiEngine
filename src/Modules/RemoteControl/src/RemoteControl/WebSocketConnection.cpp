#include "WebSocketConnection.h"

#include <RemoteControl/WebSocketHttpHead.h>

namespace BECore {

    namespace {

        constexpr int kHeaderTooLarge = 431;
        constexpr int kServiceUnavailable = 503;

    }  // namespace

    WebSocketConnection::WebSocketConnection(WebSocketConnectionId id, Socket socket, String peerAddress, WebSocketConnectionSettings settings)
        : _id(id), _peerAddress(std::move(peerAddress)), _settings(std::move(settings)), _socket(std::move(socket)) {}

    bool WebSocketConnection::IsOpen() const {
        LockGuard lock(_mutex);
        return _open && !_closing && !_aborted;
    }

    String WebSocketConnection::GetClientAddress() const {
        LockGuard lock(_mutex);
        return _clientAddress.empty() ? _peerAddress : _clientAddress;
    }

    bool WebSocketConnection::HasClientAddress() const {
        LockGuard lock(_mutex);
        return !_clientAddress.empty();
    }

    void WebSocketConnection::SetClientAddress(String address) {
        LockGuard lock(_mutex);
        _clientAddress = std::move(address);
    }

    std::expected<size_t, SocketError> WebSocketConnection::Receive(Span<uint8_t> buffer) {
        const auto received = _socket.Receive(buffer);
        if (received) {
            _bytesReceived.fetch_add(*received, std::memory_order_relaxed);
        }
        return received;
    }

    bool WebSocketConnection::Handshake(const HandshakePolicy& policy, std::chrono::milliseconds timeout, Vector<uint8_t>& pendingBytes, Optional<AdmitClientFn> admit) {
        Vector<uint8_t> received;
        const HttpHeadResult read = ReadHttpHead(
            std::chrono::steady_clock::now() + timeout, [this](std::chrono::milliseconds left) { _socket.SetReceiveTimeout(left); }, [this](Span<uint8_t> buffer) { return Receive(buffer); },
            received);
        if (read.outcome == HttpHeadOutcome::TooLong) {
            const String response = BuildRejectResponse(HandshakeError{kHeaderTooLarge, "the request head is too long"});
            (void)_socket.SendAll(AsBytes(response));
            return false;
        }
        if (read.outcome != HttpHeadOutcome::Complete) {
            return false;  // the client is gone or too slow
        }
        const size_t headSize = read.headSize;

        const StringView head(reinterpret_cast<const char*>(received.data()), headSize);
        if (IsHealthCheckRequest(head, policy)) {
            // A probe of the hosting platform: answered and closed, the handler never hears of it and nothing is logged per check
            (void)_socket.SendAll(AsBytes(BuildHealthResponse()));
            return false;
        }

        const auto key = ParseHandshakeRequest(head, policy);
        if (!key) {
            LOG_WARNING("{}: a connection was refused: {}"_format(_settings.logName, key.error().message));
            const String response = BuildRejectResponse(key.error());
            (void)_socket.SendAll(AsBytes(response));
            return false;
        }

        if (policy.trustedProxyHops != 0) {
            String clientAddress = ParseForwardedClientAddress(head, policy.trustedProxyHops);
            if (clientAddress.empty()) {
                clientAddress = _peerAddress;  // no usable entry: the proxy's own address, which is all there is to tell
            }
            if (!admit) {
                SetClientAddress(std::move(clientAddress));
            } else if (!(*admit)(clientAddress)) {
                (void)_socket.SendAll(AsBytes(BuildRejectResponse(HandshakeError{kServiceUnavailable, _settings.busyMessage})));
                return false;
            }
        }

        const String response = BuildAcceptResponse(*key);
        if (!_socket.SendAll(AsBytes(response))) {
            return false;
        }
        _socket.SetReceiveTimeout(std::chrono::milliseconds::zero());
        pendingBytes.assign(received.begin() + static_cast<ptrdiff_t>(headSize), received.end());
        return true;
    }

    void WebSocketConnection::StartWriter() {
        {
            LockGuard lock(_mutex);
            _open = true;
        }
        _writer = Thread(_settings.writerThreadName.c_str(), _settings.writerThreadOrder, [this] { WriterLoop(); });
    }

    void WebSocketConnection::EndReading() {
        bool closing = false;
        {
            LockGuard lock(_mutex);
            closing = _closing;
        }
        if (!closing) {
            Abort();
        }
        if (_writer.joinable()) {
            _writer.join();
        }
    }

    void WebSocketConnection::Abort() {
        {
            LockGuard lock(_mutex);
            _aborted = true;
            _outbox.clear();
            _outboxBytes = 0;
        }
        _socket.ShutdownBoth();
        _wake.notify_all();
    }

    void WebSocketConnection::AbortIfCloseIsOverdue(std::chrono::steady_clock::time_point now) {
        {
            LockGuard lock(_mutex);
            if (_aborted || !_closeDeadline || now < *_closeDeadline) {
                return;
            }
        }
        LOG_WARNING("{}: a client did not take its close frame, the connection is dropped"_format(_settings.logName));
        Abort();
    }

    bool WebSocketConnection::QueueFrame(Vector<uint8_t> bytes, bool isClose) {
        bool overflow = false;
        {
            LockGuard lock(_mutex);
            if (!_open || _closing || _aborted) {
                return false;
            }
            if (_outboxBytes + bytes.size() > _settings.maxOutboxBytes) {
                overflow = true;
            } else {
                _outboxBytes += bytes.size();
                _closing = isClose;
                if (isClose) {
                    _closeDeadline = std::chrono::steady_clock::now() + _settings.closeTimeout;
                }
                _outbox.push_back(OutgoingFrame{std::move(bytes), isClose});
            }
        }
        if (overflow) {
            LOG_WARNING("{}: a client does not read its messages, the connection is dropped"_format(_settings.logName));
            Abort();
            return false;
        }
        _wake.notify_one();
        return true;
    }

    void WebSocketConnection::WriterLoop() {
        for (;;) {
            OutgoingFrame frame;
            {
                UniqueLock lock(_mutex);
                _wake.wait(lock, [this] { return _aborted || !_outbox.empty(); });
                if (_aborted) {
                    return;
                }
                frame = std::move(_outbox.front());
                _outbox.pop_front();
                _outboxBytes -= frame.bytes.size();
            }

            if (!_socket.SendAll(Span<const uint8_t>(frame.bytes.data(), frame.bytes.size()))) {
                Abort();
                return;
            }
            if (frame.isClose) {
                _socket.ShutdownBoth();  // the client sees the end of the stream after the close frame
                return;
            }
        }
    }

}  // namespace BECore
