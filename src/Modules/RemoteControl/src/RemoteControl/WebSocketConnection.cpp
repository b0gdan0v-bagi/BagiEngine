#include "WebSocketConnection.h"

namespace BECore {

    namespace {

        constexpr eastl::string_view kHandshakeHeadEnd = "\r\n\r\n";
        constexpr int kHeaderTooLarge = 431;
        constexpr int kServiceUnavailable = 503;

    }  // namespace

    WebSocketConnection::WebSocketConnection(WebSocketConnectionId id, Socket socket, eastl::string peerAddress, WebSocketConnectionSettings settings)
        : _id(id), _peerAddress(std::move(peerAddress)), _settings(std::move(settings)), _socket(std::move(socket)) {}

    bool WebSocketConnection::IsOpen() const {
        LockGuard lock(_mutex);
        return _open && !_closing && !_aborted;
    }

    eastl::string WebSocketConnection::GetClientAddress() const {
        LockGuard lock(_mutex);
        return _clientAddress.empty() ? _peerAddress : _clientAddress;
    }

    bool WebSocketConnection::HasClientAddress() const {
        LockGuard lock(_mutex);
        return !_clientAddress.empty();
    }

    void WebSocketConnection::SetClientAddress(eastl::string address) {
        LockGuard lock(_mutex);
        _clientAddress = std::move(address);
    }

    std::expected<size_t, SocketError> WebSocketConnection::Receive(eastl::span<uint8_t> buffer) {
        const auto received = _socket.Receive(buffer);
        if (received) {
            _bytesReceived.fetch_add(*received, std::memory_order_relaxed);
        }
        return received;
    }

    bool WebSocketConnection::Handshake(const HandshakePolicy& policy, std::chrono::milliseconds timeout, eastl::vector<uint8_t>& pendingBytes, const AdmitClientFn& admit) {
        // One deadline for the whole head: a timeout per Receive() would let a client that sends a byte every few seconds hold the slot for ever
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        eastl::vector<uint8_t> received;
        std::array<uint8_t, 1024> chunk;
        size_t headSize = 0;
        for (;;) {
            // Zero would mean "wait for ever" to the socket, so a deadline that is (nearly) reached ends the handshake
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                return false;
            }
            _socket.SetReceiveTimeout(left);
            const auto count = Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
            if (!count) {
                return false;  // the client is gone or too slow
            }
            // Pointers, not std::array iterators: EASTL's insert does not take MSVC's class iterator
            received.insert(received.end(), chunk.data(), chunk.data() + *count);

            const eastl::string_view text(reinterpret_cast<const char*>(received.data()), received.size());
            const size_t end = text.find(kHandshakeHeadEnd);
            if (end != eastl::string_view::npos) {
                headSize = end + kHandshakeHeadEnd.size();
                break;
            }
            if (received.size() > kMaxHandshakeBytes) {
                const eastl::string response = BuildRejectResponse(HandshakeError{kHeaderTooLarge, "the request head is too long"});
                (void)_socket.SendAll(AsBytes(response));
                return false;
            }
        }

        const eastl::string_view head(reinterpret_cast<const char*>(received.data()), headSize);
        if (IsHealthCheckRequest(head, policy)) {
            // A probe of the hosting platform: answered and closed, the handler never hears of it and nothing is logged per check
            (void)_socket.SendAll(AsBytes(BuildHealthResponse()));
            return false;
        }

        const auto key = ParseHandshakeRequest(head, policy);
        if (!key) {
            LOG_WARNING("{}: a connection was refused: {}"_format(_settings.logName, key.error().message));
            const eastl::string response = BuildRejectResponse(key.error());
            (void)_socket.SendAll(AsBytes(response));
            return false;
        }

        if (policy.trustedProxyHops != 0) {
            eastl::string clientAddress = ParseForwardedClientAddress(head, policy.trustedProxyHops);
            if (clientAddress.empty()) {
                clientAddress = _peerAddress;  // no usable entry: the proxy's own address, which is all there is to tell
            }
            if (!admit) {
                SetClientAddress(std::move(clientAddress));
            } else if (!admit(clientAddress)) {
                (void)_socket.SendAll(AsBytes(BuildRejectResponse(HandshakeError{kServiceUnavailable, _settings.busyMessage})));
                return false;
            }
        }

        const eastl::string response = BuildAcceptResponse(*key);
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

    bool WebSocketConnection::QueueFrame(eastl::vector<uint8_t> bytes, bool isClose) {
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

            if (!_socket.SendAll(eastl::span<const uint8_t>(frame.bytes.data(), frame.bytes.size()))) {
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
