#include "WebSocketServer.h"

#include <BECore/Control/ControlService.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <condition_variable>

namespace BECore {

    namespace {

        constexpr std::chrono::milliseconds kAcceptPollInterval{50};  // how soon Stop() is noticed by the accept thread
        constexpr std::chrono::milliseconds kBusyDrainTimeout{100};   // how long a refused client gets to send its request before the 503
        constexpr size_t kReadChunkSize = 16 * 1024;
        constexpr eastl::string_view kHandshakeHeadEnd = "\r\n\r\n";
        constexpr int kServiceUnavailable = 503;
        constexpr int kHeaderTooLarge = 431;

        eastl::span<const uint8_t> AsBytes(const eastl::string& text) {
            return eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        /// Answers a client that cannot be served now. The request is read first: closing a socket with unread data
        /// makes the system reset the connection, and the client could lose the answer
        void RejectBusy(Socket socket) {
            socket.SetReceiveTimeout(kBusyDrainTimeout);
            std::array<uint8_t, kMaxHandshakeBytes> discarded;
            (void)socket.Receive(eastl::span<uint8_t>(discarded.data(), discarded.size()));
            const eastl::string response = BuildRejectResponse(HandshakeError{kServiceUnavailable, "the control API has too many connections"});
            (void)socket.SendAll(AsBytes(response));
        }

    }  // namespace

    /// One client: its socket, the two threads that serve it and the queue of frames waiting to be sent
    class WebSocketServer::Connection {
    public:
        explicit Connection(Socket socket) : _socket(std::move(socket)) {}

        Socket& GetSocket() {
            return _socket;
        }

        void StartReader(std::thread reader) {
            _reader = std::move(reader);
        }

        /// Joins the reader thread; the reader joins the writer before it ends
        void Join() {
            if (_reader.joinable()) {
                _reader.join();
            }
        }

        void MarkFinished() {
            _finished = true;
        }

        [[nodiscard]] bool IsFinished() const {
            return _finished;
        }

        /// Reads the HTTP request, answers it and leaves in @p pendingBytes what came after it. False: no WebSocket connection (the client was answered with an error or is gone)
        bool Handshake(eastl::vector<uint8_t>& pendingBytes);

        void StartWriter() {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _open = true;
            }
            _writer = std::thread([this] { WriterLoop(); });
        }

        /// Called by the reader when it is done: a queued close frame is sent, anything else is dropped; then the writer ends
        void EndReading() {
            bool closing = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                closing = _closing;
            }
            if (!closing) {
                Abort();
            }
            if (_writer.joinable()) {
                _writer.join();
            }
        }

        /// Queues a text frame; false when the connection takes no more frames
        bool QueueText(eastl::string_view text) {
            return QueueFrame(EncodeTextFrame(text), /*isClose=*/false);
        }

        bool QueuePong(eastl::span<const uint8_t> payload) {
            return QueueFrame(EncodeFrame(WebSocketOpcode::kPong, payload), /*isClose=*/false);
        }

        /// Queues the close frame; the connection ends after it was sent
        bool QueueClose(uint16_t code, eastl::string_view reason) {
            return QueueFrame(EncodeCloseFrame(code, reason), /*isClose=*/true);
        }

        /// Drops what is queued and ends both directions now; wakes the threads that wait on the socket or the queue
        void Abort() {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _aborted = true;
                _outbox.clear();
                _outboxBytes = 0;
            }
            _socket.ShutdownBoth();
            _wake.notify_all();
        }

    private:
        struct OutgoingFrame {
            eastl::vector<uint8_t> bytes;
            bool isClose = false;
        };

        bool QueueFrame(eastl::vector<uint8_t> bytes, bool isClose) {
            bool overflow = false;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (!_open || _closing || _aborted) {
                    return false;
                }
                if (_outboxBytes + bytes.size() > kMaxOutboxBytes) {
                    overflow = true;
                } else {
                    _outboxBytes += bytes.size();
                    _closing = isClose;
                    _outbox.push_back(OutgoingFrame{std::move(bytes), isClose});
                }
            }
            if (overflow) {
                LOG_WARNING("Control API: a client does not read its responses, the connection is dropped");
                Abort();
                return false;
            }
            _wake.notify_one();
            return true;
        }

        void WriterLoop() {
            for (;;) {
                OutgoingFrame frame;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
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

        Socket _socket;
        std::thread _reader;  // started and joined by the accept thread and Stop(), never touched by the connection's own threads
        std::thread _writer;  // started and joined by the reader thread
        std::atomic<bool> _finished{false};

        std::mutex _mutex;  // guards everything below
        std::condition_variable _wake;
        eastl::deque<OutgoingFrame> _outbox;
        size_t _outboxBytes = 0;
        bool _open = false;     // handshake done, the writer runs
        bool _closing = false;  // a close frame is queued: nothing may follow it
        bool _aborted = false;
    };

    bool WebSocketServer::Connection::Handshake(eastl::vector<uint8_t>& pendingBytes) {
        _socket.SetReceiveTimeout(kHandshakeTimeout);

        eastl::vector<uint8_t> received;
        std::array<uint8_t, 1024> chunk;
        size_t headSize = 0;
        for (;;) {
            const auto count = _socket.Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
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
        const auto key = ParseHandshakeRequest(head);
        if (!key) {
            LOG_WARNING("Control API: a connection was refused: {}"_format(key.error().message));
            const eastl::string response = BuildRejectResponse(key.error());
            (void)_socket.SendAll(AsBytes(response));
            return false;
        }

        const eastl::string response = BuildAcceptResponse(*key);
        if (!_socket.SendAll(AsBytes(response))) {
            return false;
        }
        _socket.SetReceiveTimeout(std::chrono::milliseconds::zero());
        pendingBytes.assign(received.begin() + static_cast<ptrdiff_t>(headSize), received.end());
        return true;
    }

    WebSocketServer::WebSocketServer(ControlService& service) : _service(service) {}

    WebSocketServer::~WebSocketServer() {
        Stop();
    }

    std::expected<uint16_t, ControlError> WebSocketServer::Start(uint16_t port) {
        if (IsRunning()) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kAlreadyRunning, "the control server is already running"));
        }

        auto listener = Socket::Listen(port);
        if (!listener) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kListenFailed, Format("cannot listen on 127.0.0.1:{}: {}", port, listener.error())));
        }
        _listener = std::move(*listener);
        _port = _listener.GetLocalPort();
        _stopping = false;

        _service.SetEventSink([this](const eastl::string& text) { Broadcast(text); });
        _acceptThread = std::thread([this] { AcceptLoop(); });

        LOG_INFO("Control API listening on ws://127.0.0.1:{}"_format(_port));
        return _port;
    }

    void WebSocketServer::Stop() {
        if (!IsRunning()) {
            return;
        }

        _stopping = true;
        _service.SetEventSink({});
        if (_acceptThread.joinable()) {
            _acceptThread.join();  // no connection is added after this
        }
        _listener.Close();

        eastl::vector<eastl::shared_ptr<Connection>> connections;
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            connections.swap(_connections);
        }
        for (const auto& connection : connections) {
            connection->Abort();
        }
        for (const auto& connection : connections) {
            connection->Join();
        }
        _port = 0;
        LOG_INFO("Control API stopped");
    }

    void WebSocketServer::AcceptLoop() {
        while (!_stopping) {
            auto accepted = _listener.Accept(kAcceptPollInterval);
            ReapFinishedConnections();
            if (accepted) {
                AddConnection(std::move(*accepted));
            } else if (accepted.error() == SocketError::Failed) {
                if (!_stopping) {
                    LOG_ERROR("Control API: accepting connections failed, no new client can connect");
                }
                return;
            }
        }
    }

    void WebSocketServer::AddConnection(Socket socket) {
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            if (_connections.size() < kMaxConnections) {
                auto connection = eastl::make_shared<Connection>(std::move(socket));
                _connections.push_back(connection);
                connection->StartReader(std::thread([this, connection] { RunConnection(connection); }));
                return;
            }
        }
        LOG_WARNING("Control API: a client was refused, {} connections are open"_format(kMaxConnections));
        RejectBusy(std::move(socket));
    }

    void WebSocketServer::ReapFinishedConnections() {
        eastl::vector<eastl::shared_ptr<Connection>> finished;
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            for (auto it = _connections.begin(); it != _connections.end();) {
                if ((*it)->IsFinished()) {
                    finished.push_back(*it);
                    it = _connections.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const auto& connection : finished) {
            connection->Join();
        }
    }

    void WebSocketServer::RunConnection(const eastl::shared_ptr<Connection>& connection) {
        eastl::vector<uint8_t> pendingBytes;
        if (connection->Handshake(pendingBytes)) {
            LOG_INFO("Control API: a client connected");
            connection->StartWriter();
            ReadMessages(connection, std::move(pendingBytes));
            LOG_INFO("Control API: a client disconnected");
        }
        connection->EndReading();
        connection->MarkFinished();
    }

    void WebSocketServer::ReadMessages(const eastl::shared_ptr<Connection>& connection, eastl::vector<uint8_t> pendingBytes) {
        WebSocketFrameParser parser;
        parser.Feed(eastl::span<const uint8_t>(pendingBytes.data(), pendingBytes.size()));

        std::array<uint8_t, kReadChunkSize> chunk;
        for (;;) {
            for (;;) {
                auto popped = parser.Pop();
                if (!popped) {
                    LOG_WARNING("Control API: a client broke the protocol: {}"_format(popped.error().reason));
                    connection->QueueClose(popped.error().closeCode, popped.error().reason);
                    return;
                }
                if (!popped->has_value()) {
                    break;
                }
                if (!HandleMessage(connection, std::move(**popped))) {
                    return;
                }
            }

            const auto received = connection->GetSocket().Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
            if (!received) {
                return;
            }
            parser.Feed(eastl::span<const uint8_t>(chunk.data(), *received));
        }
    }

    bool WebSocketServer::HandleMessage(const eastl::shared_ptr<Connection>& connection, WebSocketMessage message) {
        switch (message.kind) {
            case WebSocketMessageKind::Text: {
                // The response is queued by the main thread when the request has run; the connection stays alive until then
                eastl::shared_ptr<Connection> target = connection;
                _service.Post(message.GetText(), [target](eastl::string response) { target->QueueText(response); });
                return true;
            }
            case WebSocketMessageKind::Binary:
                connection->QueueClose(WebSocketCloseCode::kUnsupportedData, "binary messages are not supported");
                return false;
            case WebSocketMessageKind::Ping:
                connection->QueuePong(eastl::span<const uint8_t>(message.payload.data(), message.payload.size()));
                return true;
            case WebSocketMessageKind::Pong:
                return true;
            case WebSocketMessageKind::Close:
                connection->QueueClose(message.closeCode == WebSocketCloseCode::kNoStatusReceived ? WebSocketCloseCode::kNormal : message.closeCode, {});
                return false;
        }
        return false;
    }

    void WebSocketServer::Broadcast(const eastl::string& text) {
        eastl::vector<eastl::shared_ptr<Connection>> targets;
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            targets = _connections;
        }
        for (const auto& connection : targets) {
            connection->QueueText(text);  // a connection that is not open yet or is closing takes nothing
        }
    }

}  // namespace BECore
