#include "RoomWebSocketServer.h"

#include <Games/SeatedGameRegistry.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kLogName = "Room server";
        constexpr eastl::string_view kBusyMessage = "the server is full, try again later";
        constexpr uint32_t kThreadOrder = 210;  // the listener's threads take this and the next two, the hub thread the one after

        /// What the client is told when the hub ends its connection
        uint16_t GetCloseCode(const eastl::string& reason) {
            if (reason == "rate_limited" || reason == "too_many_failed_joins") {
                return WebSocketCloseCode::kPolicyViolation;
            }
            if (reason == "server_full") {
                return WebSocketCloseCode::kTryAgainLater;
            }
            return WebSocketCloseCode::kNormal;
        }

        WebSocketListenerSettings MakeListenerSettings(const RoomServerSettings& settings) {
            WebSocketListenerSettings listener;
            listener.bind = settings.bind;
            listener.policy = settings.policy;
            listener.maxConnections = settings.maxConnections;
            listener.maxConnectionsPerAddress = settings.maxConnectionsPerAddress;
            listener.maxMessageBytes = settings.hub.maxMessageBytes;
            listener.maxOutboxBytes = settings.maxOutboxBytes;
            listener.logName = eastl::string(kLogName);
            listener.busyMessage = eastl::string(kBusyMessage);
            listener.threadName = "Room";
            listener.threadOrder = kThreadOrder;
            return listener;
        }

    }  // namespace

    RoomWebSocketServer::RoomWebSocketServer(const SeatedGameRegistry& registry, RoomServerSettings settings)
        : _registry(registry), _settings(std::move(settings)), _clock(_settings.clock ? _settings.clock : std::function<RoomTime()>([] { return std::chrono::steady_clock::now(); })),
          _random(_settings.random ? _settings.random : MakeSecureRandom()), _listener(MakeListenerSettings(_settings), MakeHandler()) {}

    RoomWebSocketServer::~RoomWebSocketServer() {
        Stop();
    }

    WebSocketHandler RoomWebSocketServer::MakeHandler() {
        WebSocketHandler handler;
        handler.onOpen = [this](const WebSocketHandler::ConnectionPtr& connection) {
            {
                std::lock_guard<std::mutex> lock(_connectionsMutex);
                _connections[connection->GetId()] = connection;
            }
            PushEvent(EventKind::Opened, connection->GetId(), {});
        };
        handler.onMessage = [this](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessage message) {
            return PushEvent(EventKind::Message, connection->GetId(), eastl::string(message.GetText()));
        };
        handler.onClosed = [this](const WebSocketHandler::ConnectionPtr& connection) {
            {
                std::lock_guard<std::mutex> lock(_connectionsMutex);
                _connections.erase(connection->GetId());
            }
            PushEvent(EventKind::Closed, connection->GetId(), {});
        };
        return handler;
    }

    std::expected<uint16_t, ControlError> RoomWebSocketServer::Start() {
        if (IsRunning()) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kAlreadyRunning, "the room server is already running"));
        }

        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _stopping = false;
            _queue.clear();
            _queueBytes = 0;
        }
        const auto started = _listener.Start(_settings.port);
        if (!started) {
            LOG_ERROR("{}: {}"_format(kLogName, started.error().message));
            return started;
        }

        // A connection that arrives before the hub thread runs waits in the queue
        _hubThread = std::thread([this] { RunHub(); });
        LOG_INFO("{}: listening on port {}"_format(kLogName, *started));
        return started;
    }

    void RoomWebSocketServer::Stop() {
        if (!IsRunning()) {
            return;
        }

        // A reader that waits for room in the queue would never end, and the listener joins the readers: wake them first
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _stopping = true;
        }
        _notFull.notify_all();
        _hubWake.notify_all();

        _listener.Stop();
        if (_hubThread.joinable()) {
            _hubThread.join();
        }
        {
            std::lock_guard<std::mutex> lock(_connectionsMutex);
            _connections.clear();
        }
        LOG_INFO("{}: stopped"_format(kLogName));
    }

    bool RoomWebSocketServer::PushEvent(EventKind kind, ConnectionId connection, eastl::string text) {
        const size_t size = text.size();
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _notFull.wait(lock, [&] { return _stopping || (_queue.size() < kMaxQueuedEvents && (_queue.empty() || _queueBytes + size <= kMaxQueuedBytes)); });
            if (_stopping) {
                return false;
            }
            _queueBytes += size;
            _queue.push_back(Event{kind, connection, std::move(text)});
        }
        _hubWake.notify_one();
        return true;
    }

    void RoomWebSocketServer::RunHub() {
        BE_PROFILE_THREAD_ORDER("Room hub", kThreadOrder + 3);

        // The hub lives here and nowhere else: only this thread can reach it
        RoomHub hub(_registry, _settings.hub, _random,
                    HubSink{
                        .send = [this](ConnectionId connection, eastl::string text) { SendTo(connection, std::move(text)); },
                        .close = [this](ConnectionId connection, eastl::string reason) { CloseConnection(connection, reason); },
                    });

        LivenessMap liveness;
        auto nextTick = std::chrono::steady_clock::now() + _settings.tickInterval;
        eastl::deque<Event> batch;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _hubWake.wait_until(lock, nextTick, [this] { return _stopping || !_queue.empty(); });
                if (_stopping) {
                    return;
                }
                batch.swap(_queue);
                _queueBytes = 0;
            }
            _notFull.notify_all();

            for (const Event& event : batch) {
                switch (event.kind) {
                    case EventKind::Opened: {
                        const RoomTime now = Now();
                        if (const auto opened = FindConnection(event.connection)) {
                            liveness[event.connection] = Liveness{opened->GetBytesReceived(), now, false};
                        }
                        hub.OnConnected(event.connection, now);
                        break;
                    }
                    case EventKind::Message:
                        hub.OnMessage(event.connection, event.text, Now());
                        break;
                    case EventKind::Closed:
                        liveness.erase(event.connection);
                        hub.OnDisconnected(event.connection, Now());
                        break;
                }
            }
            batch.clear();

            if (std::chrono::steady_clock::now() >= nextTick) {
                const RoomTime now = Now();
                hub.Tick(now);
                CheckLiveness(liveness, now);
                nextTick = std::chrono::steady_clock::now() + _settings.tickInterval;
            }
        }
    }

    void RoomWebSocketServer::CheckLiveness(LivenessMap& liveness, RoomTime now) {
        if (_settings.pingInterval <= std::chrono::milliseconds::zero()) {
            return;
        }

        std::lock_guard<std::mutex> lock(_connectionsMutex);
        for (const auto& [id, connection] : _connections) {
            if (!connection->IsOpen()) {
                continue;  // closing or dropped: its Closed event is on the way
            }
            const uint64_t received = connection->GetBytesReceived();
            const auto [entry, isNew] = liveness.try_emplace(id, Liveness{received, now, false});
            Liveness& state = entry->second;
            if (isNew) {
                continue;
            }
            if (received != state.bytesReceived) {
                state = Liveness{received, now, false};
                continue;
            }

            const auto silence = now - state.lastLife;
            if (silence >= 2 * _settings.pingInterval) {
                LOG_INFO("{}: a connection did not answer and was dropped"_format(kLogName));
                connection->Abort();
            } else if (silence >= _settings.pingInterval && !state.pingSent) {
                state.pingSent = connection->QueuePing();
            }
        }
    }

    void RoomWebSocketServer::SendTo(ConnectionId connection, eastl::string text) {
        if (const auto target = FindConnection(connection)) {
            target->QueueText(text);  // a connection that is closing or dropped takes nothing: the hub learns it from the Closed event
        }
    }

    void RoomWebSocketServer::CloseConnection(ConnectionId connection, const eastl::string& reason) {
        if (const auto target = FindConnection(connection)) {
            LOG_INFO("{}: a connection was closed: {}"_format(kLogName, reason));
            target->QueueClose(GetCloseCode(reason), reason);
        }
    }

    eastl::shared_ptr<WebSocketConnection> RoomWebSocketServer::FindConnection(ConnectionId connection) {
        std::lock_guard<std::mutex> lock(_connectionsMutex);
        const auto found = _connections.find(connection);
        return found == _connections.end() ? nullptr : found->second;
    }

    RoomTime RoomWebSocketServer::Now() const {
        return _clock();
    }

}  // namespace BECore
