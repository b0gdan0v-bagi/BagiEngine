#include "RoomHub.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        /// How many room codes MakeUnusedRoomCode draws before it gives up (with 64 rooms in 32^5 codes a second try is already rare;
        /// a constant RandomFn must not hang the server)
        constexpr int kMaxCodeAttempts = 16;

        constexpr eastl::string_view kReasonLeft = "left";
        constexpr eastl::string_view kReasonReplaced = "replaced";
        constexpr eastl::string_view kReasonRoomClosed = "room_closed";
        constexpr eastl::string_view kReasonRoomFinished = "room_finished";
        constexpr eastl::string_view kReasonLobbyTimeout = "lobby_timeout";

        /// Tokens per second of a bucket that gives one back per @p refill (none for a refill of zero)
        double RefillRate(std::chrono::milliseconds refill) {
            return refill.count() > 0 ? 1000.0 / static_cast<double>(refill.count()) : 0.0;
        }

        bool IsJoinGuess(eastl::string_view code) {
            return code == RoomErrors::kUnknownRoom || code == RoomErrors::kBadToken;
        }

    }  // namespace

    RoomHub::RoomHub(const SeatedGameRegistry& registry, RoomHubSettings settings, RandomFn random, HubSink sink)
        : _registry(registry), _settings(settings), _random(std::move(random)), _sink(std::move(sink)), _serverJoinBudget(settings.serverJoinBurst, RefillRate(settings.serverJoinRefill)) {}

    bool RoomHub::HasRoom(eastl::string_view code) const {
        return _rooms.find(eastl::string(code)) != _rooms.end();
    }

    RoomHub::RoomEntry* RoomHub::FindRoom(eastl::string_view code) {
        const auto found = _rooms.find(eastl::string(code));
        return found == _rooms.end() ? nullptr : found->second.get();
    }

    // ---- Output --------------------------------------------------------------------------------------------------------------------

    void RoomHub::SendText(ConnectionId id, eastl::string text) {
        if (_sink.send) {
            _sink.send(id, std::move(text));
        }
    }

    void RoomHub::SendError(ConnectionId id, eastl::string_view code, eastl::string_view message, eastl::string_view ref) {
        SendText(id, MakeErrorMessage(code, message, ref));
    }

    void RoomHub::SendError(ConnectionId id, const ControlError& error, eastl::string_view ref) {
        SendText(id, MakeErrorMessage(error.code, error.message, ref));
    }

    bool RoomHub::JoinBudgetOpen(const Connection& connection, RoomTime now) const {
        if (!_serverJoinBudget.CanTake(now)) {
            return false;
        }
        if (connection.address.empty()) {
            return true;
        }
        const auto found = _addresses.find(connection.address);
        return found == _addresses.end() || found->second.joinBudget.CanTake(now);
    }

    void RoomHub::SpendJoinBudget(const Connection& connection, RoomTime now) {
        _serverJoinBudget.Spend(now);
        if (connection.address.empty()) {
            return;
        }
        if (const auto found = _addresses.find(connection.address); found != _addresses.end()) {
            found->second.joinBudget.Spend(now);
        }
    }

    size_t RoomHub::CountRoomsOf(eastl::string_view address) const {
        size_t count = 0;
        for (const auto& [code, entry] : _rooms) {
            count += entry->address == address ? 1 : 0;
        }
        return count;
    }

    void RoomHub::FailJoin(ConnectionId id, Connection& connection, const ControlError& error, RoomTime now, bool spendBudget) {
        if (spendBudget) {
            SpendJoinBudget(connection, now);
        }
        SendError(id, error, RoomMessageTypes::kJoinRoom);
        if (++connection.failedJoinsInRow >= _settings.maxFailedJoinsInRow) {
            CloseConnection(id, "too_many_failed_joins", now);
        }
    }

    // ---- Connections ---------------------------------------------------------------------------------------------------------------

    void RoomHub::OnConnected(ConnectionId connection, RoomTime now, eastl::string_view address) {
        if (_connections.size() >= _settings.maxConnections || _connections.find(connection) != _connections.end()) {
            if (_sink.close) {
                _sink.close(connection, "server_full");
            }
            return;
        }
        Connection created;
        created.limiter = RateLimiter(_settings.rateBurst, _settings.ratePerSecond);
        created.listLimiter = RateLimiter(_settings.listBurst, RefillRate(_settings.listRefill));
        created.lastActivity = now;
        if (!address.empty()) {
            created.address = eastl::string(address);
            AddressState& state = _addresses.try_emplace(created.address, AddressState{RateLimiter(_settings.addressJoinBurst, RefillRate(_settings.addressJoinRefill)), 0}).first->second;
            ++state.connections;
        }
        _connections.emplace(connection, std::move(created));
    }

    void RoomHub::OnDisconnected(ConnectionId connection, RoomTime now) {
        DropConnection(connection, now);
    }

    void RoomHub::Detach(ConnectionId id, Connection& connection) {
        if (RoomEntry* entry = FindRoom(connection.roomCode)) {
            const auto found = eastl::find(entry->connections.begin(), entry->connections.end(), id);
            if (found != entry->connections.end()) {
                entry->connections.erase(found);
            }
        }
        connection.roomCode.clear();
        connection.player = 0;
        connection.sentLobbyRevision = 0;
        connection.sentView = false;
        connection.sentGameVersion = 0;
        connection.sentSeat = -1;
    }

    void RoomHub::DropConnection(ConnectionId id, RoomTime now) {
        const auto found = _connections.find(id);
        if (found == _connections.end()) {
            return;
        }
        const eastl::string code = found->second.roomCode;
        const PlayerId player = found->second.player;
        if (const auto address = _addresses.find(found->second.address); address != _addresses.end() && address->second.connections > 0) {
            --address->second.connections;
        }
        Detach(id, found->second);
        _connections.erase(found);
        RoomEntry* entry = FindRoom(code);
        if (!entry) {
            return;
        }
        if (entry->room->GetPhase() == RoomPhase::Lobby) {
            entry->room->RemoveMember(player);
        } else {
            entry->room->MarkAway(player, now);
        }
        if (!CloseIfDone(code, now)) {
            Flush(*entry, now);
        }
    }

    void RoomHub::CloseConnection(ConnectionId id, eastl::string_view reason, RoomTime now) {
        DropConnection(id, now);
        if (_sink.close) {
            _sink.close(id, eastl::string(reason));
        }
    }

    // ---- Messages ------------------------------------------------------------------------------------------------------------------

    void RoomHub::OnMessage(ConnectionId id, eastl::string_view text, RoomTime now) {
        const auto found = _connections.find(id);
        if (found == _connections.end()) {
            return;
        }
        Connection& connection = found->second;

        if (!connection.limiter.TryTake(now)) {
            SendError(id, RoomErrors::kRateLimited, "Too many messages: slow down", {});
            if (++connection.rateLimitedInRow >= _settings.maxRateLimitedInRow) {
                CloseConnection(id, RoomErrors::kRateLimited, now);
            }
            return;
        }
        connection.rateLimitedInRow = 0;

        auto parsed = ParseClientMessage(text, _settings.maxMessageBytes);
        if (!parsed) {
            // Only `join_room` can give these two at parse time: a code or a token that cannot exist is a guess. It is answered as
            // it is (unknown_room / bad_token, not rate_limited) even when the budgets are empty, and spends a token if one is left
            if (IsJoinGuess(parsed.error().code)) {
                connection.lastActivity = now;
                FailJoin(id, connection, parsed.error(), now);
            } else {
                SendError(id, parsed.error(), {});
            }
            return;
        }
        const ClientMessage& message = *parsed;
        // A ping keeps the transport alive but does not keep an idle connection that is in no room open
        if (!std::holds_alternative<PingMessage>(message)) {
            connection.lastActivity = now;
            if (RoomEntry* entry = FindRoom(connection.roomCode)) {
                entry->lastActivity = now;
            }
        }

        std::visit(
            [&](const auto& typed) {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, CreateRoomMessage>) {
                    HandleCreate(id, connection, typed, now);
                } else if constexpr (std::is_same_v<T, JoinRoomMessage>) {
                    HandleJoin(id, connection, typed, now);
                } else if constexpr (std::is_same_v<T, ChooseSeatMessage>) {
                    HandleChooseSeat(id, connection, typed, now);
                } else if constexpr (std::is_same_v<T, StartMessage>) {
                    HandleStart(id, connection, now);
                } else if constexpr (std::is_same_v<T, ActionMessage>) {
                    HandleAction(id, connection, typed, now);
                } else if constexpr (std::is_same_v<T, LeaveMessage>) {
                    HandleLeave(id, connection, now);
                } else if constexpr (std::is_same_v<T, ListRoomsMessage>) {
                    HandleList(id, connection, typed, now);
                } else {
                    static_assert(std::is_same_v<T, PingMessage>, "a new client message needs its handler");
                    SendText(id, MakePongMessage());
                }
            },
            message);
    }

    eastl::string RoomHub::MakeUnusedRoomCode() {
        for (int attempt = 0; attempt < kMaxCodeAttempts; ++attempt) {
            eastl::string code = GenerateRoomCode(_random);
            if (!HasRoom(code)) {
                return code;
            }
        }
        return {};
    }

    void RoomHub::HandleCreate(ConnectionId id, Connection& connection, const CreateRoomMessage& message, RoomTime now) {
        const eastl::string_view ref = RoomMessageTypes::kCreateRoom;
        if (!connection.roomCode.empty()) {
            SendError(id, RoomErrors::kAlreadyInRoom, "You are already in a room: leave it first", ref);
            return;
        }
        const std::optional<SeatedGameInfo> info = _registry.Find(message.game);
        if (!info) {
            SendError(id, RoomErrors::kUnknownGame, "No such game", ref);
            return;
        }
        if (_settings.maxRoomsPerAddress != 0 && !connection.address.empty() && CountRoomsOf(connection.address) >= _settings.maxRoomsPerAddress) {
            SendError(id, RoomErrors::kTooManyRooms, "Too many rooms from your address", ref);
            return;
        }
        if (_rooms.size() >= _settings.maxRooms) {
            SendError(id, RoomErrors::kTooManyRooms, "The server has no room for another room", ref);
            return;
        }
        eastl::string code = MakeUnusedRoomCode();
        if (code.empty()) {
            SendError(id, RoomErrors::kInternalError, "No free room code could be made", ref);
            return;
        }

        const SeatedGameRegistry* registry = &_registry;
        const eastl::string gameName = info->name;
        auto entry = eastl::make_unique<RoomEntry>();
        entry->room = eastl::make_unique<Room>(code, *info, _random, [registry, gameName]() { return registry->Create(gameName); }, _settings.timings);
        entry->creation = ++_roomsCreated;
        entry->options = message.options;
        entry->room->SetLobbyOptions(message.options);
        entry->room->SetPublic(message.isPublic);
        entry->address = connection.address;
        entry->lastActivity = now;
        auto joined = entry->room->AddMember(message.name, now);
        if (!joined) {
            SendError(id, joined.error(), ref);
            return;
        }
        connection.roomCode = code;
        connection.player = joined->player;
        connection.failedJoinsInRow = 0;
        entry->connections.push_back(id);
        RoomEntry& stored = *entry;
        _rooms.emplace(code, std::move(entry));

        SendText(id, MakeJoinedMessage(code, joined->player, joined->token, joined->seat, stored.room->IsHost(joined->player)));
        Flush(stored, now);
    }

    void RoomHub::HandleList(ConnectionId id, Connection& connection, const ListRoomsMessage& message, RoomTime now) {
        const eastl::string_view ref = RoomMessageTypes::kListRooms;
        if (!connection.roomCode.empty()) {
            SendError(id, RoomErrors::kAlreadyInRoom, "You are already in a room: leave it first", ref);
            return;
        }
        const std::optional<SeatedGameInfo> info = _registry.Find(message.game);
        if (!info) {
            SendError(id, RoomErrors::kUnknownGame, "No such game", ref);
            return;
        }
        // An empty bucket is no failed join and no reason to close: the general bucket already ends a flood
        if (!connection.listLimiter.TryTake(now)) {
            SendError(id, RoomErrors::kRateLimited, "Too many room lists: wait a moment", ref);
            return;
        }

        eastl::vector<const RoomEntry*> found;
        for (const auto& [code, entry] : _rooms) {
            const Room& room = *entry->room;
            const RoomPhase phase = room.GetPhase();
            const bool listedPhase = phase == RoomPhase::Lobby || (phase == RoomPhase::Playing && info->listsStartedRooms);
            if (room.IsPublic() && listedPhase && room.GetGameInfo().name == info->name && room.GetMemberCount() < static_cast<size_t>(info->seatCount)) {
                found.push_back(entry.get());
            }
        }
        std::ranges::sort(found, [](const RoomEntry* a, const RoomEntry* b) { return a->creation < b->creation; });

        const size_t cap = eastl::min(_settings.maxListedRooms, RoomLimits::kMaxListedRooms);
        RoomListMessage list;
        list.game = info->name;
        list.more = found.size() > cap;
        for (size_t i = 0; i < found.size() && i < cap; ++i) {
            list.rooms.push_back(found[i]->room->GetListing());
        }
        SendText(id, MakeRoomListMessage(list));
    }

    void RoomHub::HandleJoin(ConnectionId id, Connection& connection, const JoinRoomMessage& message, RoomTime now) {
        if (!connection.roomCode.empty()) {
            SendError(id, RoomErrors::kAlreadyInRoom, "You are already in a room: leave it first", RoomMessageTypes::kJoinRoom);
            return;
        }
        RoomEntry* entry = FindRoom(message.code);
        if (!JoinBudgetOpen(connection, now)) {
            // The answer is the same whether the code exists or not, so an empty budget tells nothing about the rooms. A member who
            // comes back with the token of its seat is let in: no guess gets there (128 bits)
            const bool knownMember = message.IsReconnect() && entry && entry->room->FindByToken(message.token).has_value();
            if (!knownMember) {
                FailJoin(id, connection, ControlError{eastl::string(RoomErrors::kRateLimited), "Too many failed joins: wait a while"}, now, false);
                return;
            }
        }
        if (!entry) {
            FailJoin(id, connection, ControlError{eastl::string(RoomErrors::kUnknownRoom), "No room has this code"}, now);
            return;
        }
        Room& room = *entry->room;

        if (message.IsReconnect()) {
            const std::optional<PlayerId> player = room.FindByToken(message.token);
            if (!player) {
                FailJoin(id, connection, ControlError{eastl::string(RoomErrors::kBadToken), "The token is not a member's of this room"}, now);
                return;
            }
            // A second connection with the token of a live one replaces it
            for (const ConnectionId other : entry->connections) {
                const auto otherFound = _connections.find(other);
                if (otherFound != _connections.end() && otherFound->second.player == *player) {
                    SendText(other, MakeLeftMessage(kReasonReplaced));
                    Detach(other, otherFound->second);
                    CloseConnection(other, kReasonReplaced, now);
                    break;
                }
            }
            if (auto returned = room.MarkReturned(*player, message.token); !returned) {
                FailJoin(id, connection, returned.error(), now);
                return;
            }
            connection.roomCode = room.GetCode();
            connection.player = *player;
            connection.failedJoinsInRow = 0;
            entry->connections.push_back(id);
            entry->lastActivity = now;
            SendText(id, MakeJoinedMessage(room.GetCode(), *player, message.token, *room.GetSeatOf(*player), room.IsHost(*player)));
            Flush(*entry, now);
            return;
        }

        auto joined = room.AddMember(message.name, now);
        if (!joined) {
            // The code was right, so this is not a guess
            SendError(id, joined.error(), RoomMessageTypes::kJoinRoom);
            return;
        }
        connection.roomCode = room.GetCode();
        connection.player = joined->player;
        connection.failedJoinsInRow = 0;
        entry->connections.push_back(id);
        entry->lastActivity = now;
        SendText(id, MakeJoinedMessage(room.GetCode(), joined->player, joined->token, joined->seat, room.IsHost(joined->player)));
        Flush(*entry, now);
    }

    void RoomHub::HandleChooseSeat(ConnectionId id, Connection& connection, const ChooseSeatMessage& message, RoomTime now) {
        RoomEntry* entry = FindRoom(connection.roomCode);
        if (!entry) {
            SendError(id, RoomErrors::kNotInRoom, "You are not in a room", RoomMessageTypes::kChooseSeat);
            return;
        }
        if (auto chosen = entry->room->ChooseSeat(connection.player, message.seat); !chosen) {
            SendError(id, chosen.error(), RoomMessageTypes::kChooseSeat);
            return;
        }
        Flush(*entry, now);
    }

    void RoomHub::HandleStart(ConnectionId id, Connection& connection, RoomTime now) {
        RoomEntry* entry = FindRoom(connection.roomCode);
        if (!entry) {
            SendError(id, RoomErrors::kNotInRoom, "You are not in a room", RoomMessageTypes::kStart);
            return;
        }
        if (auto started = entry->room->Start(connection.player, now, entry->options); !started) {
            SendError(id, started.error(), RoomMessageTypes::kStart);
            return;
        }
        Flush(*entry, now);
    }

    void RoomHub::HandleAction(ConnectionId id, Connection& connection, const ActionMessage& message, RoomTime now) {
        RoomEntry* entry = FindRoom(connection.roomCode);
        if (!entry) {
            SendError(id, RoomErrors::kNotInRoom, "You are not in a room", RoomMessageTypes::kAction);
            return;
        }
        // The outcome is public information, but the clients learn the result from the next `view`, so it is not sent
        if (auto performed = entry->room->PerformAction(connection.player, message.version, message.action); !performed) {
            SendError(id, performed.error(), RoomMessageTypes::kAction);
            if (performed.error().code == RoomErrors::kStaleAction) {
                ResendView(id, connection, *entry);
            }
            return;
        }
        Flush(*entry, now);
    }

    void RoomHub::HandleLeave(ConnectionId id, Connection& connection, RoomTime now) {
        RoomEntry* entry = FindRoom(connection.roomCode);
        if (!entry) {
            SendError(id, RoomErrors::kNotInRoom, "You are not in a room", RoomMessageTypes::kLeave);
            return;
        }
        const eastl::string code = connection.roomCode;
        entry->room->Leave(connection.player);
        SendText(id, MakeLeftMessage(kReasonLeft));
        Detach(id, connection);
        connection.lastActivity = now;
        if (!CloseIfDone(code, now)) {
            Flush(*entry, now);
        }
    }

    // ---- Flush and closing ---------------------------------------------------------------------------------------------------------

    void RoomHub::ResendView(ConnectionId id, Connection& connection, RoomEntry& entry) {
        Room& room = *entry.room;
        const auto view = room.GetSeatView(connection.player);
        const auto actions = room.GetSeatActions(connection.player);
        const std::optional<int> seat = room.GetSeatOf(connection.player);
        if (!view || !actions || !seat) {
            return;
        }
        SendText(id, MakeViewMessage(room.GetCode(), *seat, room.GetGameVersion(), room.GetPhase() == RoomPhase::Finished, *view, *actions));
        connection.sentView = true;
        connection.sentGameVersion = room.GetGameVersion();
        connection.sentSeat = *seat;
    }

    void RoomHub::Flush(RoomEntry& entry, RoomTime now) {
        Room& room = *entry.room;
        if (room.GetPhase() == RoomPhase::Finished && !entry.finished) {
            entry.finished = true;
            entry.finishedAt = now;
        }
        const LobbySnapshot lobby = room.GetLobbySnapshot();
        const uint64_t revision = room.GetLobbyRevision();
        const bool started = room.GetPhase() != RoomPhase::Lobby;
        const eastl::string lobbyText = MakeLobbyMessage(lobby);
        // Copy: a sink is not allowed to call back, but the loop must not depend on that
        const eastl::vector<ConnectionId> ids = entry.connections;
        for (const ConnectionId id : ids) {
            const auto found = _connections.find(id);
            if (found == _connections.end()) {
                continue;
            }
            Connection& connection = found->second;
            if (connection.sentLobbyRevision != revision) {
                connection.sentLobbyRevision = revision;
                SendText(id, lobbyText);
            }
            if (started) {
                const std::optional<int> seat = room.GetSeatOf(connection.player);
                if (seat && (!connection.sentView || connection.sentGameVersion != room.GetGameVersion() || connection.sentSeat != *seat)) {
                    ResendView(id, connection, entry);
                }
            }
        }
    }

    void RoomHub::CloseRoom(const eastl::string& code, eastl::string_view reason, RoomTime now) {
        const auto found = _rooms.find(code);
        if (found == _rooms.end()) {
            return;
        }
        const eastl::vector<ConnectionId> ids = found->second->connections;
        for (const ConnectionId id : ids) {
            const auto connection = _connections.find(id);
            if (connection == _connections.end()) {
                continue;
            }
            SendText(id, MakeLeftMessage(reason));
            connection->second.roomCode.clear();
            connection->second.player = 0;
            connection->second.sentLobbyRevision = 0;
            connection->second.sentView = false;
            connection->second.sentGameVersion = 0;
            connection->second.sentSeat = -1;
            connection->second.lastActivity = now;
        }
        _rooms.erase(found);
    }

    bool RoomHub::CloseIfDone(const eastl::string& code, RoomTime now) {
        RoomEntry* entry = FindRoom(code);
        if (!entry || !entry->room->ShouldClose(now)) {
            return false;
        }
        CloseRoom(code, kReasonRoomClosed, now);
        return true;
    }

    void RoomHub::Tick(RoomTime now) {
        eastl::vector<ConnectionId> idle;
        for (const auto& [id, connection] : _connections) {
            if (connection.roomCode.empty() && now - connection.lastActivity >= _settings.idleTimeout) {
                idle.push_back(id);
            }
        }
        for (const ConnectionId id : idle) {
            CloseConnection(id, "idle", now);
        }

        // An address with no connection and a full budget is forgotten, so the map cannot grow with the clients that came and went.
        // With addressJoinRefill == 0 a spent bucket never fills again: the address stays, which is the ban that setting asks for
        for (auto it = _addresses.begin(); it != _addresses.end();) {
            if (it->second.connections == 0 && it->second.joinBudget.PeekTokens(now) >= static_cast<double>(std::max(_settings.addressJoinBurst, 1))) {
                it = _addresses.erase(it);
            } else {
                ++it;
            }
        }

        eastl::vector<eastl::string> codes;
        for (const auto& [code, entry] : _rooms) {
            codes.push_back(code);
        }
        for (const eastl::string& code : codes) {
            RoomEntry* entry = FindRoom(code);
            if (!entry) {
                continue;
            }
            if (_settings.lobbyTimeout.count() > 0 && entry->room->GetPhase() == RoomPhase::Lobby && now - entry->lastActivity >= _settings.lobbyTimeout) {
                CloseRoom(code, kReasonLobbyTimeout, now);
                continue;
            }
            entry->room->StepComputers(now);
            if (CloseIfDone(code, now)) {
                continue;
            }
            Flush(*entry, now);
            if (entry->finished && now - entry->finishedAt >= _settings.timings.abandonedTimeout) {
                CloseRoom(code, kReasonRoomFinished, now);
            }
        }
    }

}  // namespace BECore
