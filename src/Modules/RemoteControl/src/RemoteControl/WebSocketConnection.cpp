#include "WebSocketConnection.h"

#include <RemoteControl/WebSocketHttpHead.h>

namespace BECore {

    namespace {

        constexpr int kHeaderTooLarge = 431;
        constexpr int kServiceUnavailable = 503;

        /// How long a client that keeps sending after the close frame holds its connection
        constexpr std::chrono::milliseconds kCloseLinger{1000};

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
        if (_closeSent.load(std::memory_order_acquire)) {
            return std::unexpected(SocketError::Closed);  // the writer is discarding the rest: a part of a frame must not reach the parser
        }
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
            LOG_WARNING("{}: a connection was refused: {}", _settings.logName, key.error().message);
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
        LOG_WARNING("{}: a client did not take its close frame, the connection is dropped", _settings.logName);
        Abort();
    }

    IntrusivePtr<WebSocketSharedFrame> WebSocketSharedFrame::MakeText(StringView text) {
        BE_MEM_TAG(Network);
        BE_PROFILE_SCOPE("WS encode");
        Vector<uint8_t> bytes;
        AppendFrame(bytes, WebSocketOpcode::kText, Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
        return New<WebSocketSharedFrame>(std::move(bytes));
    }

    bool WebSocketConnection::QueueFrame(Vector<uint8_t> bytes, bool isClose) {
        BE_MEM_TAG(Network);
        return Enqueue(OutgoingFrame{std::move(bytes), {}, isClose});
    }

    bool WebSocketConnection::QueueShared(const IntrusivePtr<WebSocketSharedFrame>& frame) {
        BE_MEM_TAG(Network);
        BE_PROFILE_SCOPE("WS encode");
        return Enqueue(OutgoingFrame{{}, frame, false});
    }

    bool WebSocketConnection::Enqueue(OutgoingFrame frame) {
        const size_t size = frame.Bytes().size();
        bool overflow = false;
        {
            LockGuard lock(_mutex);
            if (!_open || _closing || _aborted) {
                return false;
            }
            if (_outboxBytes + size > _settings.maxOutboxBytes) {
                overflow = true;
            } else {
                _outboxBytes += size;
                _closing = frame.isClose;
                if (frame.isClose) {
                    _closeDeadline = std::chrono::steady_clock::now() + _settings.closeTimeout;
                }
                _outbox.push_back(std::move(frame));
            }
        }
        if (overflow) {
            LOG_WARNING("{}: a client does not read its messages, the connection is dropped", _settings.logName);
            Abort();
            return false;
        }
        _wake.notify_one();
        return true;
    }

    void WebSocketConnection::WriterLoop() {
        BE_MEM_TAG(Network);
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
                _outboxBytes -= frame.Bytes().size();
            }

            bool sent = false;
            {
                BE_PROFILE_SCOPE("WS write");
                sent = _socket.SendAll(frame.Bytes());
            }
            if (!sent) {
                Abort();
                return;
            }
            if (frame.isClose) {
                _closeSent.store(true, std::memory_order_release);
                _socket.FinishSending(kCloseLinger);  // the client sees the end of the stream after the close frame, and a client still sending does not lose that frame to a reset
                _socket.ShutdownBoth();
                return;
            }
        }
    }

}  // namespace BECore
