#include "RoomClient.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        [[nodiscard]] std::unexpected<ControlError> Fail(eastl::string_view code, eastl::string message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
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
        _state = RoomClientState::Connecting;
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
        if (_transport) {
            Retire();
            Emit(RoomClientEventKind::Closed, {}, "disconnected", {}, kRoomTransportNormalClosure);
        }
        ForgetRoom();
        _awaitingJoin = false;
        _state = RoomClientState::Disconnected;
    }

    void RoomClient::Retire() {
        _transport->Close(kRoomTransportNormalClosure, "disconnect");
        _retired = std::move(_transport);
        _transport.reset();
    }

    void RoomClient::Poll() {
        Poll(std::chrono::steady_clock::now());
    }

    void RoomClient::Poll(RoomTime /*now*/) {
        if (_retired) {
            while (_retired->PollEvent(_polled)) {
                if (_polled.kind == RoomTransportEventKind::Closed) {
                    _retired.reset();
                    break;
                }
            }
        }
        while (_transport && _transport->PollEvent(_polled)) {
            HandleTransportEvent(_polled);
        }
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
                _state = RoomClientState::Connected;
                Emit(RoomClientEventKind::Connected);
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
                       // A newer server may send messages this client does not know
                       [](const UnknownServerMessage&) {},
                   },
                   *parsed);
    }

    void RoomClient::OnJoined(JoinedMessage&& message) {
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
        Emit(RoomClientEventKind::Joined);
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
        Emit(RoomClientEventKind::Left, message.reason);
    }

    void RoomClient::OnTransportClosed(const RoomTransportEvent& event) {
        _transport.reset();
        _firstMessage.clear();
        _awaitingJoin = false;
        ForgetRoom();
        _state = RoomClientState::Disconnected;
        Emit(RoomClientEventKind::Closed, {}, event.reason, {}, event.closeCode);
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

    void RoomClient::Emit(RoomClientEventKind kind, eastl::string_view code, eastl::string_view message, eastl::string_view ref, uint16_t closeCode) {
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
    }

}  // namespace BECore
