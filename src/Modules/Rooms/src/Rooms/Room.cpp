#include "Room.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

namespace BECore {

    namespace {

        ControlError MakeError(eastl::string_view code, eastl::string_view message) {
            return ControlError{eastl::string(code), eastl::string(message)};
        }

    }  // namespace

    Room::Room(eastl::string code, SeatedGameInfo info, RandomFn random) : _code(std::move(code)), _info(std::move(info)), _random(std::move(random)) {}

    const Room::Member* Room::FindMember(PlayerId player) const {
        for (const Member& member : _members) {
            if (member.id == player) {
                return &member;
            }
        }
        return nullptr;
    }

    int Room::FindLowestFreeSeat() const {
        for (int seat = 0; seat < _info.seatCount; ++seat) {
            if (!GetPlayerAt(seat)) {
                return seat;
            }
        }
        return -1;
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
        const int seat = FindLowestFreeSeat();
        if (seat < 0) {
            return std::unexpected(MakeError(RoomErrors::kRoomFull, "The room is full"));
        }

        eastl::string token = GenerateToken(_random);
        while (FindByToken(token)) {
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
        if (_phase != RoomPhase::Lobby) {
            return false;
        }
        for (size_t i = 0; i < _members.size(); ++i) {
            if (_members[i].id == player) {
                _members.erase(_members.begin() + static_cast<ptrdiff_t>(i));
                if (_host == player) {
                    PassHost();
                }
                BumpLobbyRevision();
                return true;
            }
        }
        return false;
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
        const Member* found = FindMember(player);
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
        for (Member& member : _members) {
            if (member.id == player) {
                member.seat = target;
            }
        }
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
        return lobby;
    }

}  // namespace BECore
