#pragma once

#include <BECore/Control/ControlTypes.h>
#include <Games/SeatedGameRegistry.h>
#include <Rooms/RoomProtocol.h>

namespace BECore {

    /// What AddMember gives the new member
    struct RoomMemberJoined {
        PlayerId player = 0;
        /// The reconnect secret: goes to its owner only
        eastl::string token;
        int seat = 0;
    };

    /**
     * @brief One room: who sits where, who hosts, and where the room is in its life.
     *
     * Pure logic over PlayerId: no connections (the hub maps those), no JSON of its own (the hub makes the messages from
     * GetLobbySnapshot), no clock and no randomness of its own (both are passed in). Not thread-safe: the caller serializes calls.
     *
     * Everybody who joins takes the lowest free seat at once, so every member has a seat and there are no spectators.
     * Strings that come from a client (names, tokens) are checked here again: the room does not rely on the parser.
     *
     * This part is the lobby; the start of the game, the play and the absence of players are added to the same class.
     */
    class Room {
    public:
        Room(eastl::string code, SeatedGameInfo info, RandomFn random);

        /**
         * @brief A new member takes the lowest free seat. The first member is the host.
         *
         * @param name Untrusted; sanitised again (SanitizePlayerName). Names may repeat: the seat tells people apart.
         * @return `bad_name`, `game_started` (the room is no longer in the lobby: only a member can return, by token), `room_full`.
         */
        [[nodiscard]] std::expected<RoomMemberJoined, ControlError> AddMember(eastl::string_view name, RoomTime now);

        /// Removes a member of a room in the lobby: the seat is free again, and when it was the host, the host passes to the human
        /// of the lowest seat. False (and nothing changes) for an unknown player or a room that has started.
        bool RemoveMember(PlayerId player);

        /// The member that has @p token. Compares in time that does not depend on how much of the token is right. Untrusted input.
        [[nodiscard]] std::optional<PlayerId> FindByToken(eastl::string_view token) const;

        /**
         * @brief Moves a member of a room in the lobby to a free seat.
         *
         * @param seat Untrusted, any integer.
         * @return `not_in_room` (unknown player), `game_started`, `invalid_seat` (outside 0..seatCount-1), `seat_taken` (by another
         *         member). The seat the member already has is a success that changes nothing.
         */
        [[nodiscard]] std::expected<void, ControlError> ChooseSeat(PlayerId player, int64_t seat);

        [[nodiscard]] bool IsHost(PlayerId player) const;
        [[nodiscard]] std::optional<int> GetSeatOf(PlayerId player) const;
        /// The player on @p seat, or nullopt when nobody is
        [[nodiscard]] std::optional<PlayerId> GetPlayerAt(int seat) const;
        [[nodiscard]] bool HasMember(PlayerId player) const;

        /// What a `lobby` message says now
        [[nodiscard]] LobbySnapshot GetLobbySnapshot() const;

        /// Grows with every change that GetLobbySnapshot shows (a join, a leave, a seat, the host, the phase); a call that changes
        /// nothing leaves it as it is. Never goes back.
        [[nodiscard]] uint64_t GetLobbyRevision() const {
            return _lobbyRevision;
        }

        [[nodiscard]] RoomPhase GetPhase() const {
            return _phase;
        }

        /// True when no member is in the room
        [[nodiscard]] bool IsEmpty() const {
            return _members.empty();
        }

        [[nodiscard]] size_t GetMemberCount() const {
            return _members.size();
        }

        [[nodiscard]] const eastl::string& GetCode() const {
            return _code;
        }

        [[nodiscard]] const SeatedGameInfo& GetGameInfo() const {
            return _info;
        }

    private:
        struct Member {
            PlayerId id = 0;
            eastl::string name;
            eastl::string token;
            int seat = 0;
            /// Whether the connection of the member is up (always true in the lobby: a dropped connection removes the member)
            bool connected = true;
        };

        [[nodiscard]] const Member* FindMember(PlayerId player) const;
        [[nodiscard]] int FindLowestFreeSeat() const;
        void PassHost();
        void BumpLobbyRevision();

        eastl::string _code;
        SeatedGameInfo _info;
        RandomFn _random;
        RoomPhase _phase = RoomPhase::Lobby;
        eastl::vector<Member> _members;
        PlayerId _nextPlayer = 1;
        /// 0 when no human is left
        PlayerId _host = 0;
        uint64_t _lobbyRevision = 1;
    };

}  // namespace BECore
