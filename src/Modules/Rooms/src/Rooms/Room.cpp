#include "Room.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        ControlError MakeError(eastl::string_view code, eastl::string_view message) {
            return ControlError{eastl::string(code), eastl::string(message)};
        }

        /// How many tokens AddMember draws before it gives up (a collision of 128 bits never happens, a constant RandomFn does)
        constexpr int kMaxTokenAttempts = 16;

    }  // namespace

    Room::Room(eastl::string code, SeatedGameInfo info, RandomFn random, SeatedGameFactory factory, RoomTimings timings)
        : _code(std::move(code)), _info(std::move(info)), _random(std::move(random)), _factory(std::move(factory)), _timings(timings) {}

    const Room::Member* Room::FindMember(PlayerId player) const {
        for (const Member& member : _members) {
            if (member.id == player) {
                return &member;
            }
        }
        return nullptr;
    }

    Room::Member* Room::FindMember(PlayerId player) {
        for (Member& member : _members) {
            if (member.id == player) {
                return &member;
            }
        }
        return nullptr;
    }

    std::optional<int> Room::FindLowestFreeSeat() const {
        for (int seat = 0; seat < _info.seatCount; ++seat) {
            if (!GetPlayerAt(seat)) {
                return seat;
            }
        }
        return std::nullopt;
    }

    void Room::BumpLobbyRevision() {
        ++_lobbyRevision;
    }

    std::expected<RoomMemberJoined, ControlError> Room::AddMember(eastl::string_view name, RoomTime /*now*/) {
        std::optional<eastl::string> cleanName = SanitizePlayerName(name);
        if (!cleanName) {
            return std::unexpected(MakeError(RoomErrors::kBadName, "The name must be 1 to 24 bytes of printable text"));
        }
        if (_phase != RoomPhase::Lobby) {
            return std::unexpected(MakeError(RoomErrors::kGameStarted, "The game has started: only a member can return, with the token"));
        }
        const std::optional<int> freeSeat = FindLowestFreeSeat();
        if (!freeSeat) {
            return std::unexpected(MakeError(RoomErrors::kRoomFull, "The room is full"));
        }
        const int seat = *freeSeat;

        eastl::string token = GenerateToken(_random);
        for (int attempt = 1; FindByToken(token); ++attempt) {
            if (attempt >= kMaxTokenAttempts) {
                return std::unexpected(MakeError(RoomErrors::kInternalError, "No free token could be made"));
            }
            token = GenerateToken(_random);
        }

        Member member;
        member.id = _nextPlayer++;
        member.name = std::move(*cleanName);
        member.token = token;
        member.seat = seat;
        const PlayerId id = member.id;
        _members.push_back(std::move(member));
        if (_host == 0) {
            _host = id;
        }
        BumpLobbyRevision();
        return RoomMemberJoined{id, std::move(token), seat};
    }

    bool Room::RemoveMember(PlayerId player) {
        if (_phase != RoomPhase::Lobby || !HasMember(player)) {
            return false;
        }
        EraseMember(player);
        return true;
    }

    void Room::EraseMember(PlayerId player) {
        for (size_t i = 0; i < _members.size(); ++i) {
            if (_members[i].id == player) {
                _members.erase(_members.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
        if (_host == player) {
            PassHost();
        }
        BumpLobbyRevision();
    }

    bool Room::Leave(PlayerId player) {
        if (!HasMember(player)) {
            return false;
        }
        // In a game the seat has no member any more, so a computer plays it for good
        EraseMember(player);
        return true;
    }

    void Room::PassHost() {
        _host = 0;
        int bestSeat = _info.seatCount;
        for (const Member& member : _members) {
            if (member.connected && member.seat < bestSeat) {
                bestSeat = member.seat;
                _host = member.id;
            }
        }
    }

    std::expected<void, ControlError> Room::Start(PlayerId host, RoomTime now, const Json& options) {
        if (!HasMember(host)) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (!IsHost(host)) {
            return std::unexpected(MakeError(RoomErrors::kNotHost, "Only the host can start the game"));
        }
        if (_phase != RoomPhase::Lobby) {
            return std::unexpected(MakeError(RoomErrors::kGameStarted, "The game has already started"));
        }
        eastl::unique_ptr<ISeatedGame> game = _factory ? _factory() : nullptr;
        if (!game || game->GetSeatCount() != _info.seatCount) {
            return std::unexpected(MakeError(RoomErrors::kInternalError, "The game could not be created"));
        }
        if (auto started = game->Start(_random(), options); !started) {
            return std::unexpected(std::move(started.error()));
        }
        _game = std::move(game);
        _phase = RoomPhase::Playing;
        _lastComputerMove = now;
        BumpLobbyRevision();
        FinishIfDone();
        return {};
    }

    void Room::FinishIfDone() {
        if (_phase == RoomPhase::Playing && _game && _game->IsFinished()) {
            _phase = RoomPhase::Finished;
            BumpLobbyRevision();
        }
    }

    uint64_t Room::GetGameVersion() const {
        return _game ? _game->GetStateVersion() : 0;
    }

    std::expected<Json, ControlError> Room::PerformAction(PlayerId player, uint64_t version, const Json& action) {
        const Member* member = FindMember(player);
        if (!member) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (!_game) {
            return std::unexpected(MakeError(RoomErrors::kNotStarted, "The game has not started"));
        }
        if (version != _game->GetStateVersion()) {
            return std::unexpected(MakeError(RoomErrors::kStaleAction, "The game has moved on: look at the new view"));
        }
        if (!member->connected) {
            return std::unexpected(MakeError(RoomErrors::kNotYourTurn, "The player is away"));
        }
        auto outcome = _game->PerformSeatAction(member->seat, action);
        if (outcome) {
            FinishIfDone();
        }
        return outcome;
    }

    std::expected<Json, ControlError> Room::GetSeatView(PlayerId player) const {
        const Member* member = FindMember(player);
        if (!member) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (!_game) {
            return std::unexpected(MakeError(RoomErrors::kNotStarted, "The game has not started"));
        }
        return _game->GetSeatView(member->seat);
    }

    std::expected<Json, ControlError> Room::GetSeatActions(PlayerId player) const {
        const Member* member = FindMember(player);
        if (!member) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (!_game) {
            return std::unexpected(MakeError(RoomErrors::kNotStarted, "The game has not started"));
        }
        return _game->GetSeatActions(member->seat);
    }

    bool Room::MarkAway(PlayerId player, RoomTime now) {
        Member* member = FindMember(player);
        if (!member || _phase == RoomPhase::Lobby) {
            return false;
        }
        if (member->connected) {
            member->connected = false;
            member->awaySince = now;
            BumpLobbyRevision();
        }
        return true;
    }

    std::expected<void, ControlError> Room::MarkReturned(PlayerId player, eastl::string_view token) {
        Member* member = FindMember(player);
        if (!member) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (FindByToken(token) != player) {
            return std::unexpected(MakeError(RoomErrors::kBadToken, "The token is not this player's"));
        }
        if (!member->connected) {
            member->connected = true;
            if (_host == 0) {
                PassHost();
            }
            BumpLobbyRevision();
        }
        return {};
    }

    bool Room::IsSeatComputerControlled(int seat, RoomTime now) const {
        if (_phase == RoomPhase::Lobby || seat < 0 || seat >= _info.seatCount) {
            return false;
        }
        const std::optional<PlayerId> player = GetPlayerAt(seat);
        if (!player) {
            return true;
        }
        const Member* member = FindMember(*player);
        return !member->connected && now - member->awaySince >= _timings.reconnectGrace;
    }

    bool Room::StepComputers(RoomTime now) {
        if (_phase != RoomPhase::Playing || !_game || now - _lastComputerMove < _timings.computerDelay) {
            return false;
        }
        for (int seat = 0; seat < _info.seatCount; ++seat) {
            if (!IsSeatComputerControlled(seat, now)) {
                continue;
            }
            const std::optional<Json> action = _game->ChooseComputerAction(seat);
            if (!action) {
                continue;
            }
            if (auto performed = _game->PerformSeatAction(seat, *action); !performed) {
                // No room code, name or game message in the log: the platform's log is not a place for them (RoomServer/CLAUDE.md, "Log")
                LOG_ERROR(Format("Room: the game refused the computer's action for seat {}: {}", seat, performed.error().code.c_str()));
                return false;
            }
            _lastComputerMove = now;
            FinishIfDone();
            return true;
        }
        return false;
    }

    bool Room::ShouldClose(RoomTime now) const {
        if (_members.empty()) {
            return true;
        }
        if (_phase == RoomPhase::Lobby) {
            return false;
        }
        for (const Member& member : _members) {
            if (member.connected || now - member.awaySince < _timings.abandonedTimeout) {
                return false;
            }
        }
        return true;
    }

    std::optional<PlayerId> Room::FindByToken(eastl::string_view token) const {
        // Every member is compared over the whole length of the longer text, with no early exit, so the time says nothing about
        // how many characters are right or which member is nearly right
        std::optional<PlayerId> found;
        for (const Member& member : _members) {
            const size_t length = std::max(member.token.size(), token.size());
            unsigned difference = member.token.size() != token.size() ? 1u : 0u;
            for (size_t i = 0; i < length; ++i) {
                const unsigned a = i < member.token.size() ? static_cast<unsigned char>(member.token[i]) : 0u;
                const unsigned b = i < token.size() ? static_cast<unsigned char>(token[i]) : 0u;
                difference |= a ^ b;
            }
            if (difference == 0) {
                found = member.id;
            }
        }
        return found;
    }

    std::expected<void, ControlError> Room::ChooseSeat(PlayerId player, int64_t seat) {
        Member* found = FindMember(player);
        if (!found) {
            return std::unexpected(MakeError(RoomErrors::kNotInRoom, "You are not in this room"));
        }
        if (_phase != RoomPhase::Lobby) {
            return std::unexpected(MakeError(RoomErrors::kGameStarted, "The seats cannot change once the game has started"));
        }
        if (seat < 0 || seat >= _info.seatCount) {
            return std::unexpected(MakeError(RoomErrors::kInvalidSeat, "No such seat"));
        }
        const int target = static_cast<int>(seat);
        if (found->seat == target) {
            return {};
        }
        if (GetPlayerAt(target)) {
            return std::unexpected(MakeError(RoomErrors::kSeatTaken, "The seat is taken"));
        }
        found->seat = target;
        BumpLobbyRevision();
        return {};
    }

    bool Room::IsHost(PlayerId player) const {
        return player != 0 && player == _host;
    }

    std::optional<int> Room::GetSeatOf(PlayerId player) const {
        const Member* member = FindMember(player);
        if (!member) {
            return std::nullopt;
        }
        return member->seat;
    }

    std::optional<PlayerId> Room::GetPlayerAt(int seat) const {
        for (const Member& member : _members) {
            if (member.seat == seat) {
                return member.id;
            }
        }
        return std::nullopt;
    }

    bool Room::HasMember(PlayerId player) const {
        return FindMember(player) != nullptr;
    }

    void Room::SetLobbyOptions(const Json& options) {
        Json kept = Json::object();
        if (options.is_object()) {
            for (const eastl::string& key : _info.publicOptions) {
                if (kept.size() >= RoomLimits::kMaxLobbyOptions) {
                    break;
                }
                const Json* value = FindJsonMember(options, key);
                if (value != nullptr && (value->is_boolean() || value->is_number_integer())) {
                    kept[std::string(key.data(), key.size())] = *value;
                }
            }
        }
        if (kept != _lobbyOptions) {
            _lobbyOptions = std::move(kept);
            BumpLobbyRevision();
        }
    }

    LobbySnapshot Room::GetLobbySnapshot() const {
        LobbySnapshot lobby;
        lobby.room = _code;
        lobby.game = _info.name;
        lobby.phase = _phase;
        lobby.hostSeat = -1;
        lobby.seats.reserve(static_cast<size_t>(_info.seatCount));
        for (int seat = 0; seat < _info.seatCount; ++seat) {
            LobbySeat entry;
            entry.seat = seat;
            const std::optional<PlayerId> player = GetPlayerAt(seat);
            if (player) {
                const Member* member = FindMember(*player);
                entry.kind = SeatKind::Human;
                entry.name = member->name;
                entry.connected = member->connected;
                if (*player == _host) {
                    lobby.hostSeat = seat;
                }
            } else {
                // Once the game has started an empty seat is played by a computer
                entry.kind = _phase == RoomPhase::Lobby ? SeatKind::Empty : SeatKind::Computer;
            }
            lobby.seats.push_back(std::move(entry));
        }
        lobby.options = _lobbyOptions;
        return lobby;
    }

    RoomListing Room::GetListing() const {
        RoomListing listing;
        listing.room = _code;
        if (const Member* host = FindMember(_host)) {
            listing.host = host->name;
        }
        listing.taken = static_cast<int>(_members.size());
        listing.seats = _info.seatCount;
        listing.started = _phase != RoomPhase::Lobby;
        listing.options = _lobbyOptions;
        return listing;
    }

}  // namespace BECore
