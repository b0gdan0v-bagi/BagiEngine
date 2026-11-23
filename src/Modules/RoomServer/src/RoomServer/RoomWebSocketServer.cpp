#include "RoomWebSocketServer.h"

#include <Games/SeatedGameRegistry.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        constexpr StringView kLogName = "Room server";
        constexpr StringView kBusyMessage = "the server is full, try again later";
        constexpr StringView kStoppingReason = "server_stopping";
        constexpr uint32_t kThreadOrder = 210;  // the listener's threads take this and the next two, the hub thread the one after

        WebSocketListenerSettings MakeListenerSettings(const RoomServerSettings& settings) {
            WebSocketListenerSettings listener;
            listener.bind = settings.bind;
            listener.policy = settings.policy;
            listener.maxConnections = settings.maxConnections;
            listener.maxConnectionsPerAddress = settings.maxConnectionsPerAddress;
            listener.maxMessageBytes = settings.hub.maxMessageBytes;
            listener.maxOutboxBytes = settings.maxOutboxBytes;
            listener.closeTimeout = std::max(listener.closeTimeout, settings.shutdownGrace);  // Stop() gives its clients the whole grace
            listener.logName = String(kLogName);
            listener.busyMessage = String(kBusyMessage);
            listener.threadName = "Room";
            listener.threadOrder = kThreadOrder;
            return listener;
        }

    }  // namespace

    uint16_t GetRoomCloseCode(StringView reason) {
        if (reason == RoomCloseReasons::kRateLimited || reason == RoomCloseReasons::kTooManyFailedJoins) {
            return WebSocketCloseCode::kPolicyViolation;
        }
        if (reason == RoomCloseReasons::kServerFull) {
            return WebSocketCloseCode::kTryAgainLater;
        }
        return WebSocketCloseCode::kNormal;
    }

    bool ShouldLogRoomServerStats(const RoomServerStats& current, const RoomServerStats& lastLogged, RoomTime now, RoomTime lastLoggedAt, std::chrono::milliseconds interval) {
        return interval > std::chrono::milliseconds::zero() && current != lastLogged && now - lastLoggedAt >= interval;
    }

    RoomWebSocketServer::RoomWebSocketServer(const SeatedGameRegistry& registry, RoomServerSettings settings)
        : _registry(registry), _clock(settings.clock ? std::move(settings.clock) : MoveOnlyFunction<RoomTime()>([] { return std::chrono::steady_clock::now(); })),
          _random(settings.random ? std::move(settings.random) : MakeSecureRandom()), _settings(std::move(settings)),
          _passAddress(_settings.maxConnectionsPerAddress != 0 || _settings.policy.trustedProxyHops != 0), _listener(MakeListenerSettings(_settings), MakeHandler()) {}

    RoomWebSocketServer::~RoomWebSocketServer() {
        Stop();
    }

    WebSocketHandler RoomWebSocketServer::MakeHandler() {
        WebSocketHandler handler;
        handler.onOpen = [this](const WebSocketHandler::ConnectionPtr& connection) {
            bool draining = false;
            {
                // The flag and the map change under one lock, so a connection is either in the snapshot Stop() closes or sees the flag here
                LockGuard lock(_connectionsMutex);
                _connections[connection->GetId()] = connection;
                draining = _draining;
            }
            if (draining) {
                connection->QueueClose(WebSocketCloseCode::kGoingAway, kStoppingReason);
            }
            PushEvent(EventKind::Opened, connection->GetId(), _passAddress ? connection->GetClientAddress() : String());
        };
        handler.onMessage = [this](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessageView message) {
            return PushEvent(EventKind::Message, connection->GetId(), String(message.GetText()));
        };
        handler.onClosed = [this](const WebSocketHandler::ConnectionPtr& connection) {
            {
                LockGuard lock(_connectionsMutex);
                _connections.erase(connection->GetId());
            }
            _connectionsChanged.notify_all();
            PushEvent(EventKind::Closed, connection->GetId(), {});
        };
        return handler;
    }

    std::expected<uint16_t, ControlError> RoomWebSocketServer::Start() {
        if (IsRunning()) {
            return std::unexpected(MakeControlError(RemoteControlErrors::kAlreadyRunning, "the room server is already running"));
        }

        {
            LockGuard lock(_queueMutex);
            _stopping = false;
            _queue.clear();
            _queueBytes = 0;
        }
        {
            LockGuard lock(_connectionsMutex);
            _draining = false;
        }
        const auto started = _listener.Start(_settings.port);
        if (!started) {
            LOG_ERROR("{}: {}", kLogName, started.error().message);
            return started;
        }

        // A connection that arrives before the hub thread runs waits in the queue
        _hubThread = Thread("Room hub", kThreadOrder + 3, [this] { RunHub(); });
        LOG_INFO("{}: listening on port {}", kLogName, *started);
        return started;
    }

    void RoomWebSocketServer::Stop() {
        if (!IsRunning()) {
            return;
        }

        // The hub thread and the readers go on working while the clients are told: the hub takes the `Closed` events, so its rooms see the players leave
        if (_settings.shutdownGrace > std::chrono::milliseconds::zero()) {
            DrainConnections();
        }

        // A reader that waits for room in the queue would never end, and the listener joins the readers: wake them first
        {
            LockGuard lock(_queueMutex);
            _stopping = true;
        }
        _notFull.notify_all();
        _hubWake.notify_all();

        _listener.Stop();
        if (_hubThread.joinable()) {
            _hubThread.join();
        }
        {
            LockGuard lock(_connectionsMutex);
            _connections.clear();
            _draining = false;
        }
        LOG_INFO("{}: stopped", kLogName);
    }

    void RoomWebSocketServer::DrainConnections() {
        Vector<WebSocketHandler::ConnectionPtr> open;
        {
            LockGuard lock(_connectionsMutex);
            _draining = true;
            open.reserve(_connections.size());
            for (const auto& entry : _connections) {
                open.push_back(entry.second);
            }
        }
        if (open.empty()) {
            return;
        }

        LOG_INFO("{}: stopping, {} connections are told", kLogName, open.size());
        for (const auto& connection : open) {
            connection->QueueClose(WebSocketCloseCode::kGoingAway, kStoppingReason);  // false for one that is closing or gone already: it ends by itself
        }

        // The client's answer is not waited for as such: after its close frame a connection shuts its socket and ends. What can take longer is a client that
        // does not read (its writer is stuck in a full send buffer), and the grace bounds that. The real clock: this is the process's wait, not the hub's time
        size_t left = 0;
        {
            UniqueLock lock(_connectionsMutex);
            _connectionsChanged.wait_for(lock, _settings.shutdownGrace, [this] { return _connections.empty(); });
            left = _connections.size();
        }
        if (left > 0) {
            LOG_INFO("{}: {} connections did not end within {} ms and are cut", kLogName, left, _settings.shutdownGrace.count());
        }
    }

    bool RoomWebSocketServer::PushEvent(EventKind kind, ConnectionId connection, String text) {
        const size_t size = text.size();
        {
            UniqueLock lock(_queueMutex);
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
        BE_MEM_TAG(Network);
        // The hub lives here and nowhere else: only this thread can reach it
        RoomHub hub(
            _registry, _settings.hub, [this] { return _random(); },
            HubSink{
                .send = [this](ConnectionId connection, const String& text) { SendTo(connection, text); },
                .broadcast = [this](Span<const ConnectionId> ids, const String& text) { BroadcastTo(ids, text); },
                .close = [this](ConnectionId connection, String reason) { CloseConnection(connection, reason); },
            });

        LivenessMap liveness;
        RoomServerStats lastLoggedStats;
        RoomTime lastStatsLoggedAt = Now();
        auto nextTick = std::chrono::steady_clock::now() + _settings.tickInterval;
        Fifo<Event> batch;
        for (;;) {
            {
                UniqueLock lock(_queueMutex);
                _hubWake.wait_until(lock, nextTick, [this] { return _stopping || !_queue.empty(); });
                if (_stopping) {
                    return;
                }
                batch.swap(_queue);
                _queueBytes = 0;
            }
            _notFull.notify_all();

            for (size_t i = 0; i < batch.size(); ++i) {
                const Event& event = batch[i];
                switch (event.kind) {
                    case EventKind::Opened: {
                        BE_PROFILE_SCOPE("Room open");
                        const RoomTime now = Now();
                        if (const auto opened = FindConnection(event.connection)) {
                            liveness[event.connection] = Liveness{opened->GetBytesReceived(), now, false};
                        }
                        hub.OnConnected(event.connection, now, event.text);
                        break;
                    }
                    case EventKind::Message: {
                        BE_PROFILE_SCOPE("Room message");
                        hub.OnMessage(event.connection, event.text, Now());
                        break;
                    }
                    case EventKind::Closed: {
                        BE_PROFILE_SCOPE("Room close");
                        liveness.erase(event.connection);
                        hub.OnDisconnected(event.connection, Now());
                        break;
                    }
                }
            }
            // Keeps the capacity: the swap above hands it to the queue and takes the queue's back
            batch.clear();

            if (std::chrono::steady_clock::now() >= nextTick) {
                BE_PROFILE_SCOPE("Room tick");
                const RoomTime now = Now();
                hub.Tick(now);
                CheckLiveness(liveness, now);
                LogStats(hub, lastLoggedStats, lastStatsLoggedAt, now);
                nextTick = std::chrono::steady_clock::now() + _settings.tickInterval;
            }
        }
    }

    void RoomWebSocketServer::LogStats(const RoomHub& hub, RoomServerStats& lastLogged, RoomTime& lastLoggedAt, RoomTime now) const {
        const RoomServerStats current{hub.GetConnectionCount(), hub.GetRoomCount()};
        if (!ShouldLogRoomServerStats(current, lastLogged, now, lastLoggedAt, _settings.statsLogInterval)) {
            return;
        }
        // Counts only: no name, code or token of anyone goes into a log line
        LOG_INFO("{}: {} connections, {} rooms", kLogName, current.connections, current.rooms);
        lastLogged = current;
        lastLoggedAt = now;
    }

    void RoomWebSocketServer::CheckLiveness(LivenessMap& liveness, RoomTime now) {
        if (_settings.pingInterval <= std::chrono::milliseconds::zero()) {
            return;
        }

        LockGuard lock(_connectionsMutex);
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
                LOG_INFO("{}: a connection did not answer and was dropped", kLogName);
                connection->Abort();
            } else if (silence >= _settings.pingInterval && !state.pingSent) {
                state.pingSent = connection->QueuePing();
            }
        }
    }

    void RoomWebSocketServer::SendTo(ConnectionId connection, const String& text) {
        BE_PROFILE_SCOPE("Room send");
        if (const auto target = FindConnection(connection)) {
            target->QueueText(text);  // a connection that is closing or dropped takes nothing: the hub learns it from the Closed event
        }
    }

    void RoomWebSocketServer::BroadcastTo(Span<const ConnectionId> ids, const String& text) {
        BE_PROFILE_SCOPE("Room send");
        _broadcastTargets.clear();
        {
            LockGuard lock(_connectionsMutex);
            for (const ConnectionId id : ids) {
                if (const auto found = _connections.find(id); found != _connections.end()) {
                    _broadcastTargets.push_back(found->second);
                }
            }
        }
        if (!_broadcastTargets.empty()) {
            // The bytes are encoded once and shared by the outboxes, so they outlive this call: unlike `text`, which the hub reuses
            const auto frame = WebSocketSharedFrame::MakeText(text);
            for (const auto& target : _broadcastTargets) {
                target->QueueShared(frame);
            }
        }
        // The connections are not held longer than the call
        _broadcastTargets.clear();
    }

    void RoomWebSocketServer::CloseConnection(ConnectionId connection, const String& reason) {
        BE_PROFILE_SCOPE("Room send");
        if (const auto target = FindConnection(connection)) {
            LOG_INFO("{}: a connection was closed: {}", kLogName, reason);
            target->QueueClose(GetRoomCloseCode(reason), reason);
        }
    }

    WebSocketHandler::ConnectionPtr RoomWebSocketServer::FindConnection(ConnectionId connection) {
        LockGuard lock(_connectionsMutex);
        const auto found = _connections.find(connection);
        return found == _connections.end() ? WebSocketHandler::ConnectionPtr() : found->second;
    }

    RoomTime RoomWebSocketServer::Now() const {
        return _clock();
    }

}  // namespace BECore
