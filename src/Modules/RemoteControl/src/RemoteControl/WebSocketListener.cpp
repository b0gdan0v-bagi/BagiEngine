#include "WebSocketListener.h"

namespace BECore {

    namespace {

        constexpr std::chrono::milliseconds kAcceptPollInterval{50};  // how soon Stop() is noticed by the accept thread
        constexpr std::chrono::milliseconds kBusyDrainTimeout{100};   // how long a refused client gets to send its request before the 503
        constexpr size_t kReadChunkSize = 16 * 1024;
        constexpr int kServiceUnavailable = 503;

        /// Answers a client that cannot be served now. The request is read first: closing a socket with unread data
        /// makes the system reset the connection, and the client could lose the answer
        void RejectBusy(Socket socket, const eastl::string& message) {
            socket.SetReceiveTimeout(kBusyDrainTimeout);
            std::array<uint8_t, kMaxHandshakeBytes> discarded;
            (void)socket.Receive(eastl::span<uint8_t>(discarded.data(), discarded.size()));
            const eastl::string response = BuildRejectResponse(HandshakeError{kServiceUnavailable, message});
            (void)socket.SendAll(AsBytes(response));
        }

        [[nodiscard]] eastl::string MakeThreadName(const WebSocketListenerSettings& settings, eastl::string_view role) {
            return Format("{} {}", settings.threadName, role);
        }

    }  // namespace

    WebSocketListener::WebSocketListener(WebSocketListenerSettings settings, WebSocketHandler handler) : _settings(std::move(settings)), _handler(std::move(handler)) {}

    WebSocketListener::~WebSocketListener() {
        Stop();
    }

    std::expected<uint16_t, ControlError> WebSocketListener::Start(uint16_t port) {
        if (IsRunning()) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kAlreadyRunning, "the server is already running"));
        }

        auto listener = Socket::Listen(port, _settings.bind);
        if (!listener) {
            const char* address = _settings.bind == SocketBind::Loopback ? "127.0.0.1" : "0.0.0.0";
            return std::unexpected(MakeControlError(RemoteControlErrors::kListenFailed, Format("cannot listen on {}:{}: {}", address, port, listener.error())));
        }
        _listener = std::move(*listener);
        _port = _listener.GetLocalPort();
        _stopping = false;

        _acceptThread = std::thread([this] {
            BE_PROFILE_THREAD_ORDER(MakeThreadName(_settings, "accept").c_str(), _settings.threadOrder);
            AcceptLoop();
        });
        return _port;
    }

    void WebSocketListener::Stop() {
        if (!IsRunning()) {
            return;
        }

        _stopping = true;
        if (_acceptThread.joinable()) {
            _acceptThread.join();  // no connection is added after this
        }
        _listener.Close();

        eastl::vector<ConnectionPtr> connections;
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
    }

    void WebSocketListener::ForEachOpenConnection(const std::function<void(WebSocketConnection&)>& visit) {
        eastl::vector<ConnectionPtr> targets;
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            targets = _connections;
        }
        for (const auto& connection : targets) {
            if (connection->IsOpen()) {
                visit(*connection);
            }
        }
    }

    void WebSocketListener::AcceptLoop() {
        while (!_stopping) {
            eastl::string peerAddress;
            auto accepted = _listener.Accept(kAcceptPollInterval, &peerAddress);
            ReapFinishedConnections();
            if (accepted) {
                AddConnection(std::move(*accepted), std::move(peerAddress));
            } else if (accepted.error() == SocketError::Failed) {
                if (!_stopping) {
                    LOG_ERROR("{}: accepting connections failed, no new client can connect"_format(_settings.logName));
                }
                return;
            }
        }
    }

    void WebSocketListener::AddConnection(Socket socket, eastl::string peerAddress) {
        bool perAddressLimit = false;
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            size_t open = 0;
            size_t fromAddress = 0;
            for (const auto& other : _connections) {
                if (!other->IsFinished()) {
                    ++open;
                    fromAddress += other->GetPeerAddress() == peerAddress ? 1 : 0;
                }
            }
            // Behind trusted proxies every socket has a proxy's address: the limit is checked after the handshake, by the client's
            perAddressLimit = _settings.policy.trustedProxyHops == 0 && _settings.maxConnectionsPerAddress != 0 && fromAddress >= _settings.maxConnectionsPerAddress;
            if (open < _settings.maxConnections && !perAddressLimit) {
                WebSocketConnectionSettings connectionSettings;
                connectionSettings.maxOutboxBytes = _settings.maxOutboxBytes;
                connectionSettings.logName = _settings.logName;
                connectionSettings.busyMessage = _settings.busyMessage;
                connectionSettings.writerThreadName = MakeThreadName(_settings, "writer");
                connectionSettings.writerThreadOrder = _settings.threadOrder + 2;

                auto connection = eastl::make_shared<WebSocketConnection>(_nextId++, std::move(socket), std::move(peerAddress), std::move(connectionSettings));
                _connections.push_back(connection);
                connection->StartReader(std::thread([this, connection] {
                    BE_PROFILE_THREAD_ORDER(MakeThreadName(_settings, "reader").c_str(), _settings.threadOrder + 1);
                    RunConnection(connection);
                }));
                return;
            }
        }
        if (perAddressLimit) {
            LOG_WARNING("{}: a client was refused, {} connections are open from {}"_format(_settings.logName, _settings.maxConnectionsPerAddress, peerAddress));
        } else {
            LOG_WARNING("{}: a client was refused, {} connections are open"_format(_settings.logName, _settings.maxConnections));
        }
        RejectBusy(std::move(socket), _settings.busyMessage);
    }

    bool WebSocketListener::AdmitByClientAddress(WebSocketConnection& connection, const eastl::string& clientAddress) {
        // Counting the others and setting the address are one step under the lock, so two clients of one address that shake hands at
        // the same moment cannot both pass a limit of one. A connection that has not been admitted yet (still shaking hands, or just
        // refused) has no client address and counts for nobody
        std::lock_guard<std::mutex> lock(_connectionsMutex);
        size_t fromAddress = 0;
        for (const auto& other : _connections) {
            if (other.get() != &connection && !other->IsFinished() && other->HasClientAddress() && other->GetClientAddress() == clientAddress) {
                ++fromAddress;
            }
        }
        if (_settings.maxConnectionsPerAddress != 0 && fromAddress >= _settings.maxConnectionsPerAddress) {
            LOG_WARNING("{}: a client was refused, {} connections are open from {}"_format(_settings.logName, _settings.maxConnectionsPerAddress, clientAddress));
            return false;
        }
        connection.SetClientAddress(clientAddress);
        return true;
    }

    void WebSocketListener::ReapFinishedConnections() {
        eastl::vector<ConnectionPtr> finished;
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

    void WebSocketListener::RunConnection(const ConnectionPtr& connection) {
        eastl::vector<uint8_t> pendingBytes;
        AdmitClientFn admit;
        if (_settings.policy.trustedProxyHops != 0) {
            admit = [this, &connection](const eastl::string& clientAddress) { return AdmitByClientAddress(*connection, clientAddress); };
        }
        if (connection->Handshake(_settings.policy, _settings.handshakeTimeout, pendingBytes, admit)) {
            LOG_INFO("{}: a client connected"_format(_settings.logName));
            connection->StartWriter();
            if (_handler.onOpen) {
                _handler.onOpen(connection);
            }
            ReadMessages(connection, std::move(pendingBytes));
            connection->EndReading();
            LOG_INFO("{}: a client disconnected"_format(_settings.logName));
            if (_handler.onClosed) {
                _handler.onClosed(connection);
            }
        } else {
            connection->EndReading();
        }
        connection->MarkFinished();
    }

    void WebSocketListener::ReadMessages(const ConnectionPtr& connection, eastl::vector<uint8_t> pendingBytes) {
        WebSocketFrameParser parser(_settings.maxMessageBytes);
        parser.Feed(eastl::span<const uint8_t>(pendingBytes.data(), pendingBytes.size()));

        std::array<uint8_t, kReadChunkSize> chunk;
        for (;;) {
            for (;;) {
                auto popped = parser.Pop();
                if (!popped) {
                    LOG_WARNING("{}: a client broke the protocol: {}"_format(_settings.logName, popped.error().reason));
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

            const auto received = connection->Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
            if (!received) {
                return;
            }
            parser.Feed(eastl::span<const uint8_t>(chunk.data(), *received));
        }
    }

    bool WebSocketListener::HandleMessage(const ConnectionPtr& connection, WebSocketMessage message) {
        switch (message.kind) {
            case WebSocketMessageKind::Text:
                return _handler.onMessage ? _handler.onMessage(connection, std::move(message)) : true;
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

}  // namespace BECore
