#include "RoomClient.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        [[nodiscard]] std::unexpected<ControlError> Fail(eastl::string_view code, eastl::string message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        /// The message that takes a seat back: `join_room` with the token and no name
        [[nodiscard]] eastl::string MakeResumeMessage(const eastl::string& room, const eastl::string& token) {
            JoinRoomMessage message;
            message.code = room;
            message.token = token;
            return SerializeClientMessage(message);
        }

    }  // namespace

    RoomClient::RoomClient(RoomClientSettings settings) : _settings(std::move(settings)) {}

    RoomClient::~RoomClient() = default;

    std::expected<void, ControlError> RoomClient::Host(eastl::string_view url, eastl::string_view game, eastl::string_view name, const Json& options) {
        CreateRoomMessage message;
        message.game = eastl::string(game);
        message.name = eastl::string(name);
        message.options = options;
        return BeginRequest(url, SerializeClientMessage(message));
    }

    std::expected<void, ControlError> RoomClient::Join(eastl::string_view url, eastl::string_view code, eastl::string_view name) {
        const auto normalized = NormalizeRoomCode(code);
        if (!normalized) {
            // A code that cannot exist: the server would count it as a failed guess, so it is not sent
            return Fail(RoomErrors::kUnknownRoom, "no room has this code");
        }
        JoinRoomMessage message;
        message.code = *normalized;
        message.name = eastl::string(name);
        return BeginRequest(url, SerializeClientMessage(message));
    }

    std::expected<void, ControlError> RoomClient::BeginRequest(eastl::string_view url, eastl::string firstMessage) {
        if (_state == RoomClientState::Connected && !_awaitingJoin && url == _url) {
            // The connection that is open serves the request
            _awaitingJoin = true;
            if (!_transport->SendText(firstMessage)) {
                _awaitingJoin = false;
                return Fail(RoomClientErrors::kWrongState, "the connection is closing");
            }
            return {};
        }
        if (_state != RoomClientState::Disconnected) {
            return Fail(RoomClientErrors::kWrongState, "the client is connected: leave the room or disconnect first");
        }
        if (auto started = StartConnection(url, std::move(firstMessage)); !started) {
            return started;
        }
        _endReason.clear();
        _state = RoomClientState::Connecting;
        return {};
    }

    std::expected<void, ControlError> RoomClient::StartConnection(eastl::string_view url, eastl::string firstMessage) {
        eastl::unique_ptr<IRoomTransport> transport = _settings.transportFactory ? _settings.transportFactory() : MakeDefaultRoomTransport();
        if (!transport) {
            return Fail(RoomClientErrors::kTransportUnavailable, "there is no way to connect to a room server in this build");
        }
        if (auto opened = transport->Open(url); !opened) {
            _lastError = MakeControlError(RoomClientErrors::kConnectFailed, opened.error());
            return std::unexpected(_lastError);
        }
        // An older connection that is still closing is not waited for
        _retired.reset();
        _transport = std::move(transport);
        _url = eastl::string(url);
        _firstMessage = std::move(firstMessage);
        _transportOpened = false;
        _connectStart.reset();
        return {};
    }

    std::expected<void, ControlError> RoomClient::Resume(const RoomSession& session) {
        const auto code = NormalizeRoomCode(session.room);
        if (!code || !IsWellFormedToken(session.token) || session.url.empty()) {
            return Fail(RoomClientErrors::kInvalidSession, "the session is not a room and a token on an address");
        }
        if (_state != RoomClientState::Disconnected) {
            return Fail(RoomClientErrors::kWrongState, "the client is connected: leave the room or disconnect first");
        }
        if (auto started = StartConnection(session.url, MakeResumeMessage(*code, session.token)); !started) {
            return started;
        }
        _endReason.clear();
        _session = session;
        _session.room = *code;
        _room = *code;
        ClearReconnect();
        _state = RoomClientState::Reconnecting;
        return {};
    }

    std::expected<void, ControlError> RoomClient::RequireRoom() const {
        if (_state != RoomClientState::InRoom) {
            return Fail(RoomClientErrors::kWrongState, "not in a room");
        }
        return {};
    }

    std::expected<void, ControlError> RoomClient::SendToServer(const ClientMessage& message) {
        if (!_transport->SendText(SerializeClientMessage(message))) {
            return Fail(RoomClientErrors::kWrongState, "the connection is closing");
        }
        return {};
    }

    std::expected<void, ControlError> RoomClient::ChooseSeat(int seat) {
        if (auto inRoom = RequireRoom(); !inRoom) {
            return inRoom;
        }
        if (auto sent = SendToServer(ChooseSeatMessage{seat}); !sent) {
            return sent;
        }
        _seatBeforeRequest = _seat;
        _seat = seat;
        return {};
    }

    std::expected<void, ControlError> RoomClient::Start() {
        if (auto inRoom = RequireRoom(); !inRoom) {
            return inRoom;
        }
        return SendToServer(StartMessage{});
    }

    std::expected<void, ControlError> RoomClient::SendAction(const Json& action) {
        if (auto inRoom = RequireRoom(); !inRoom) {
            return inRoom;
        }
        if (!_hasView) {
            return Fail(RoomClientErrors::kWrongState, "no view yet: the game has not started");
        }
        ActionMessage message;
        message.version = _viewVersion;
        message.action = action;
        return SendToServer(message);
    }

    std::expected<void, ControlError> RoomClient::Leave() {
        if (_state == RoomClientState::Reconnecting) {
            // There is no connection to ask on; the player is leaving, so the seat is not asked for again
            _session = {};
            Disconnect();
            return {};
        }
        if (auto inRoom = RequireRoom(); !inRoom) {
            return inRoom;
        }
        if (auto sent = SendToServer(LeaveMessage{}); !sent) {
            return sent;
        }
        _session = {};
        return {};
    }

    void RoomClient::Disconnect() {
        const bool active = _transport || _state == RoomClientState::Reconnecting;
        if (_transport) {
            Retire();
        }
        if (active) {
            Emit(RoomClientEventKind::Closed, {}, "disconnected", {}, kRoomTransportNormalClosure);
        }
        ResetConnection();
        ClearReconnect();
        ForgetRoom();
        _endReason.clear();
        _state = RoomClientState::Disconnected;
    }

    void RoomClient::Retire() {
        _transport->Close(kRoomTransportNormalClosure, "disconnect");
        _retired = std::move(_transport);
        _transport.reset();
    }

    void RoomClient::RetireAborted() {
        _transport->Abort();
        _retired = std::move(_transport);
        _transport.reset();
    }

    void RoomClient::Poll() {
        Poll(std::chrono::steady_clock::now());
    }

    void RoomClient::Poll(RoomTime now) {
        _now = now;
        if (_retired) {
            while (_retired->PollEvent(_polled)) {
                if (_polled.kind == RoomTransportEventKind::Closed) {
                    _retired.reset();
                    break;
                }
            }
        }
        if (_state == RoomClientState::Reconnecting) {
            if (!_lostAt) {
                // A Resume(): the loss is now
                _lostAt = _now;
            }
            if (!_transport && _retryAt && _now >= *_retryAt) {
                StartAttempt();
            }
        }
        while (_transport && _transport->PollEvent(_polled)) {
            HandleTransportEvent(_polled);
        }
        CheckTimers();
    }

    void RoomClient::DrainEvents(eastl::vector<RoomClientEvent>& out) {
        if (_events.empty()) {
            return;
        }
        out.insert(out.end(), eastl::make_move_iterator(_events.begin()), eastl::make_move_iterator(_events.end()));
        _events.clear();
    }

    void RoomClient::HandleTransportEvent(const RoomTransportEvent& event) {
        switch (event.kind) {
            case RoomTransportEventKind::Opened:
                _transportOpened = true;
                _connectStart.reset();
                _lastReceived = _now;
                _pingOutstanding = false;
                if (_state == RoomClientState::Connecting) {
                    _state = RoomClientState::Connected;
                    Emit(RoomClientEventKind::Connected);
                }
                if (!_firstMessage.empty()) {
                    _awaitingJoin = true;
                    if (!_transport->SendText(_firstMessage)) {
                        // The connection ended already: its Closed is next
                        _awaitingJoin = false;
                    }
                    _firstMessage.clear();
                }
                break;
            case RoomTransportEventKind::Text:
                HandleText(event.text);
                break;
            case RoomTransportEventKind::Closed:
                OnTransportClosed(event);
                break;
        }
    }

    void RoomClient::HandleText(eastl::string_view text) {
        // Any text is life, one that does not parse too
        _lastReceived = _now;
        _pingOutstanding = false;
        auto parsed = ParseServerMessage(text, RoomLimits::kMaxServerMessageBytes);
        if (!parsed) {
            LOG_WARNING(Format("Room client: a message from the server was dropped: {}", parsed.error().message));
            _lastError = MakeControlError(RoomClientErrors::kBadServerMessage, parsed.error().message);
            Emit(RoomClientEventKind::Error, _lastError.code, _lastError.message);
            return;
        }
        std::visit(overload{
                       [&](JoinedMessage& message) { OnJoined(std::move(message)); },
                       [&](LobbyMessage& message) { OnLobby(std::move(message)); },
                       [&](ViewMessage& message) { OnView(std::move(message)); },
                       [&](ErrorMessage& message) { OnError(std::move(message)); },
                       [&](LeftMessage& message) { OnLeft(std::move(message)); },
                       [](const PongMessage&) {},
                       // The browse of the open rooms comes with RoomClient::ListRooms
                       [](const RoomListMessage&) {},
                       // A newer server may send messages this client does not know
                       [](const UnknownServerMessage&) {},
                   },
                   *parsed);
    }

    void RoomClient::OnJoined(JoinedMessage&& message) {
        const bool resumed = _state == RoomClientState::Reconnecting;
        ForgetRoom();
        _awaitingJoin = false;
        _state = RoomClientState::InRoom;
        _room = std::move(message.room);
        _player = message.player;
        _seat = message.seat;
        _host = message.host;
        _session.url = _url;
        _session.room = _room;
        _session.token = std::move(message.token);
        if (resumed) {
            ClearReconnect();
            Emit(RoomClientEventKind::Resumed);
        } else {
            Emit(RoomClientEventKind::Joined);
        }
    }

    void RoomClient::OnLobby(LobbyMessage&& message) {
        if (_state != RoomClientState::InRoom || message.room != _room) {
            LOG_WARNING("Room client: a lobby message outside the room was dropped");
            return;
        }
        _lobby = std::move(message);
        _host = _lobby.hostSeat >= 0 && _lobby.hostSeat == _seat;
        Emit(RoomClientEventKind::LobbyChanged);
    }

    void RoomClient::OnView(ViewMessage&& message) {
        if (_state != RoomClientState::InRoom || message.room != _room) {
            LOG_WARNING("Room client: a view message outside the room was dropped");
            return;
        }
        _hasView = true;
        _seat = message.seat;
        _viewVersion = message.version;
        _finished = message.finished;
        _view = std::move(message.view);
        _actions = std::move(message.actions);
        _host = _lobby.hostSeat >= 0 && _lobby.hostSeat == _seat;
        Emit(RoomClientEventKind::ViewChanged);
    }

    void RoomClient::OnError(ErrorMessage&& message) {
        if (_state == RoomClientState::Reconnecting && _awaitingJoin) {
            // The server refused to take the token back. Whatever the reason, asking again would not change it
            _lastError = MakeControlError(message.code, message.message);
            const bool gone = message.code == RoomErrors::kBadToken || message.code == RoomErrors::kUnknownRoom;
            GiveUp(gone ? RoomClientCloseReasons::kRoomGone : eastl::string_view(message.code), message.message, gone);
            return;
        }
        _awaitingJoin = false;
        if (message.ref == RoomMessageTypes::kChooseSeat) {
            _seat = _seatBeforeRequest;
        }
        _lastError = MakeControlError(message.code, message.message);
        Emit(RoomClientEventKind::Error, message.code, message.message, message.ref);
    }

    void RoomClient::OnLeft(LeftMessage&& message) {
        ForgetRoom();
        _session = {};
        _awaitingJoin = false;
        if (_state == RoomClientState::InRoom) {
            _state = RoomClientState::Connected;
        }
        if (eastl::string_view(message.reason) == RoomClientCloseReasons::kReplaced) {
            // The server closes this connection next; another device has the seat and this one does not take it back
            _endReason = message.reason;
        }
        Emit(RoomClientEventKind::Left, message.reason);
    }

    void RoomClient::OnTransportClosed(const RoomTransportEvent& event) {
        _transport.reset();
        OnConnectionEnded(event.closeCode, event.reason);
    }

    void RoomClient::ResetConnection() {
        _firstMessage.clear();
        _awaitingJoin = false;
        _transportOpened = false;
        _connectStart.reset();
        _pingOutstanding = false;
    }

    void RoomClient::ClearReconnect() {
        _lostAt.reset();
        _retryAt.reset();
        _failedAttempts = 0;
    }

    void RoomClient::OnConnectionEnded(uint16_t closeCode, eastl::string_view reason, eastl::string_view endCode) {
        const RoomClientState before = _state;
        ResetConnection();
        if (before != RoomClientState::InRoom && before != RoomClientState::Reconnecting) {
            // No room to bring back
            ForgetRoom();
            _state = RoomClientState::Disconnected;
            Emit(RoomClientEventKind::Closed, endCode.empty() ? eastl::string_view(_endReason) : endCode, reason, {}, closeCode);
            _endReason.clear();
            return;
        }

        // The server closes with 1008 a client that broke its limits: retrying would be the same abuse
        const bool policy = closeCode == kRoomTransportPolicyViolation;
        const bool retry = _settings.reconnect && _session.IsValid() && !policy;
        if (before == RoomClientState::InRoom) {
            _lostAt = _now;
            _failedAttempts = 0;
            Emit(RoomClientEventKind::ConnectionLost, {}, reason, {}, closeCode, retry);
        }
        if (!retry) {
            ForgetRoom();
            ClearReconnect();
            _state = RoomClientState::Disconnected;
            const eastl::string_view code = !policy ? eastl::string_view() : (reason.empty() ? RoomClientCloseReasons::kRateLimited : reason);
            Emit(RoomClientEventKind::Closed, code, reason, {}, closeCode);
            return;
        }
        _state = RoomClientState::Reconnecting;
        ScheduleRetry();
    }

    std::chrono::milliseconds RoomClient::GetBackoff() const {
        std::chrono::milliseconds wait = _settings.backoffStart;
        for (int i = 0; i < _failedAttempts && wait < _settings.backoffMax; ++i) {
            wait *= 2;
        }
        return std::min(wait, _settings.backoffMax);
    }

    void RoomClient::ScheduleRetry() {
        _retryAt = _now + GetBackoff();
        ++_failedAttempts;
    }

    void RoomClient::StartAttempt() {
        _retryAt.reset();
        if (auto started = StartConnection(_url, MakeResumeMessage(_session.room, _session.token)); !started) {
            // An address or a platform that cannot connect does not get better by waiting
            GiveUp(started.error().code, started.error().message, false);
            return;
        }
        _connectStart = _now;
    }

    void RoomClient::GiveUp(eastl::string_view code, eastl::string_view message, bool forgetSession, uint16_t closeCode) {
        if (_transport) {
            Retire();
        }
        ResetConnection();
        ClearReconnect();
        ForgetRoom();
        if (forgetSession) {
            _session = {};
        }
        _endReason.clear();
        _state = RoomClientState::Disconnected;
        Emit(RoomClientEventKind::Closed, code, message, {}, closeCode);
    }

    void RoomClient::CheckTimers() {
        if (_state == RoomClientState::Reconnecting && _lostAt && _now - *_lostAt >= _settings.reconnectGiveUp) {
            GiveUp(RoomClientCloseReasons::kGaveUp, "the connection did not come back in time", true);
            return;
        }
        if (!_transport) {
            return;
        }
        if (!_transportOpened) {
            if (!_connectStart) {
                _connectStart = _now;
            } else if (_now - *_connectStart >= _settings.connectTimeout) {
                RetireAborted();
                OnConnectionEnded(kRoomTransportAbnormalClosure, "the connection did not open in time", RoomClientCloseReasons::kConnectTimeout);
            }
            return;
        }
        const auto silent = _now - _lastReceived;
        if (_settings.silenceLimit.count() > 0 && silent >= _settings.silenceLimit) {
            RetireAborted();
            OnConnectionEnded(kRoomTransportAbnormalClosure, "nothing came from the server");
            return;
        }
        if (_settings.pingInterval.count() > 0 && !_pingOutstanding && silent >= _settings.pingInterval) {
            // One at a time: the next waits for something to arrive. A send that fails means the connection is closing, which is reported
            _pingOutstanding = true;
            _transport->SendText(SerializeClientMessage(PingMessage{}));
        }
    }

    void RoomClient::ForgetRoom() {
        _room.clear();
        _player = 0;
        _seat = -1;
        _seatBeforeRequest = -1;
        _host = false;
        _lobby = {};
        _hasView = false;
        _view = Json::object();
        _actions = Json::array();
        _viewVersion = 0;
        _finished = false;
    }

    void RoomClient::Emit(RoomClientEventKind kind, eastl::string_view code, eastl::string_view message, eastl::string_view ref, uint16_t closeCode, bool willRetry) {
        // The state is in the getters, so a change that is already announced and not yet taken is announced once
        const bool coalesces = kind == RoomClientEventKind::LobbyChanged || kind == RoomClientEventKind::ViewChanged;
        if (coalesces && !_events.empty() && _events.back().kind == kind) {
            return;
        }
        if (_events.size() >= kMaxQueuedRoomClientEvents) {
            _events.erase(_events.begin());
        }
        RoomClientEvent& event = _events.emplace_back();
        event.kind = kind;
        event.code = eastl::string(code);
        event.message = eastl::string(message);
        event.ref = eastl::string(ref);
        event.closeCode = closeCode;
        event.willRetry = willRetry;
    }

}  // namespace BECore
