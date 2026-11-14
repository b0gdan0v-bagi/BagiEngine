#pragma once

#include <BECore/Control/ControlTypes.h>
#include <Games/SeatedGameRegistry.h>
#include <Rooms/RoomLimits.h>
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
     * The room is in the lobby until the host calls Start(); then it owns the game (made by the factory) and plays it. Every call that
     * acts for a person names the PlayerId. A seat nobody holds (an empty one, one whose player left, one whose player has been away
     * longer than the grace) is played by a computer through StepComputers().
     */
    class Room {
    public:
        /// @param factory Makes the game at Start(); a room made without one can only stay in the lobby (Start gives `internal_error`).
        Room(eastl::string code, SeatedGameInfo info, RandomFn random, SeatedGameFactory factory = {}, RoomTimings timings = {});

        /**
         * @brief A new member takes the lowest free seat. The first member is the host.
         *
         * @param name Untrusted; sanitised again (SanitizePlayerName). Names may repeat: the seat tells people apart.
         * @return `bad_name`, `game_started` (the room is no longer in the lobby: only a member can return, by token), `room_full`,
         *         `internal_error` (no free token in a bounded number of tries: a broken RandomFn).
         */
        [[nodiscard]] std::expected<RoomMemberJoined, ControlError> AddMember(StringView name, RoomTime now);

        /// Removes a member of a room in the lobby: the seat is free again, and when it was the host, the host passes to the human
        /// of the lowest seat. False (and nothing changes) for an unknown player or a room that has started (see Leave).
        bool RemoveMember(PlayerId player);

        /**
         * @brief Starts the game. Empty seats become computers.
         *
         * @param host Must be the host.
         * @param options The game's options (from `create_room`, untrusted), handed to ISeatedGame::Start as they are.
         * @return `not_in_room`, `not_host`, `game_started` (already started), `internal_error` (no factory, the factory gave no
         *         game or one with another seat count than the registry said), or the error of ISeatedGame::Start. On an error the
         *         room stays in the lobby, unchanged.
         */
        [[nodiscard]] std::expected<void, ControlError> Start(PlayerId host, RoomTime now, const Json& options = Json::object());

        /**
         * @brief A member plays an action for its seat.
         *
         * @param version The game version the member saw; must equal GetGameVersion().
         * @param action Untrusted; checked by the game.
         * @return the outcome of the action (public information), or `not_in_room`, `not_started`, `stale_action`, `not_your_turn`
         *         (also for a member who is away), or the game's own error (`illegal_action`, `invalid_params`). Nothing changes on an
         *         error. When the action ends the game the phase becomes Finished.
         */
        [[nodiscard]] std::expected<Json, ControlError> PerformAction(PlayerId player, uint64_t version, const Json& action);

        /// What the member's seat may see now (`not_in_room`, `not_started`)
        [[nodiscard]] std::expected<Json, ControlError> GetSeatView(PlayerId player) const;

        /// What the member's seat may send now (`not_in_room`, `not_started`)
        [[nodiscard]] std::expected<Json, ControlError> GetSeatActions(PlayerId player) const;

        /// The version of the game, 0 before the start
        [[nodiscard]] uint64_t GetGameVersion() const;

        /// The connection of a member dropped. Only in a room that has started (in the lobby the member is removed instead); false
        /// for an unknown player or in the lobby. The seat is kept for the grace, then a computer plays it.
        bool MarkAway(PlayerId player, RoomTime now);

        /// The member returns with its token: control of the seat comes back at once. When no connected human was left to host, the
        /// returning member becomes the host. `not_in_room`, `bad_token`.
        [[nodiscard]] std::expected<void, ControlError> MarkReturned(PlayerId player, StringView token);

        /// True when a computer plays @p seat now: nobody holds it, or its player has been away for the grace or longer. Never in the lobby.
        [[nodiscard]] bool IsSeatComputerControlled(int seat, RoomTime now) const;

        /**
         * @brief Makes one move for a computer-controlled seat, if the game wants one and the delay has passed.
         *
         * The delay counts from the start and from the last computer move. The move goes through ISeatedGame::PerformSeatAction, like a
         * person's, and is atomic. A computer choice that the game refuses is logged as an error and not retried in this call (the
         * call gives false and tries no other seat), so a misbehaving game cannot loop.
         *
         * @return whether a move was made
         */
        bool StepComputers(RoomTime now);

        /// A member leaves for good. In the lobby it is removed (RemoveMember); in a game the seat is played by a computer from now on.
        /// False for an unknown player.
        bool Leave(PlayerId player);

        /// True when the room should be closed: in the lobby when it is empty; after the start when no human is left, or every human
        /// has been away for the abandoned timeout or longer.
        [[nodiscard]] bool ShouldClose(RoomTime now) const;

        /// The member that has @p token. Compares in time that does not depend on how much of the token is right. Untrusted input.
        [[nodiscard]] Optional<PlayerId> FindByToken(StringView token) const;

        /**
         * @brief Moves a member of a room in the lobby to a free seat.
         *
         * @param seat Untrusted, any integer.
         * @return `not_in_room` (unknown player), `game_started`, `invalid_seat` (outside 0..seatCount-1), `seat_taken` (by another
         *         member). The seat the member already has is a success that changes nothing.
         */
        [[nodiscard]] std::expected<void, ControlError> ChooseSeat(PlayerId player, int64_t seat);

        [[nodiscard]] bool IsHost(PlayerId player) const;
        [[nodiscard]] Optional<int> GetSeatOf(PlayerId player) const;
        /// The player on @p seat, or nullopt when nobody is
        [[nodiscard]] Optional<PlayerId> GetPlayerAt(int seat) const;
        [[nodiscard]] bool HasMember(PlayerId player) const;

        /// What a `lobby` message says now
        [[nodiscard]] LobbySnapshot GetLobbySnapshot() const;

        /// Whether the room is listed for `list_rooms` (the host's choice at `create_room`). Not part of the lobby snapshot, so
        /// it never moves the lobby revision. A room is private until this is set.
        void SetPublic(bool isPublic) {
            _public = isPublic;
        }

        [[nodiscard]] bool IsPublic() const {
            return _public;
        }

        /// What a `rooms` entry says now: the code, the host's name (empty when no human is left to host), the humans, the seat
        /// count, whether the game has started, the public options. Whether the room may be listed is the hub's to judge.
        [[nodiscard]] RoomListing GetListing() const;

        /**
         * @brief Keeps the public part of the room's `options` (from `create_room`, untrusted) for the lobby: the members whose key is
         *        in `SeatedGameInfo::publicOptions` and whose value is a boolean or an integer, at most kMaxLobbyOptions. Anything
         *        else is left out, never refused (the game judges the options at `Start`). Bumps the lobby revision when it changed.
         */
        void SetLobbyOptions(const Json& options);

        /// Grows with every change that GetLobbySnapshot shows (a join, a leave, a seat, the host, the phase, an away or a return); a call
        /// that changes nothing leaves it as it is. Never goes back.
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
            /// Since when the connection is down; meaningful only while connected is false
            RoomTime awaySince{};
        };

        [[nodiscard]] const Member* FindMember(PlayerId player) const;
        [[nodiscard]] Member* FindMember(PlayerId player);
        [[nodiscard]] Optional<int> FindLowestFreeSeat() const;
        void EraseMember(PlayerId player);
        void FinishIfDone();
        void PassHost();
        void BumpLobbyRevision();

        eastl::string _code;
        SeatedGameInfo _info;
        RandomFn _random;
        SeatedGameFactory _factory;
        RoomTimings _timings;
        eastl::unique_ptr<ISeatedGame> _game;
        RoomTime _lastComputerMove{};
        RoomPhase _phase = RoomPhase::Lobby;
        eastl::vector<Member> _members;
        PlayerId _nextPlayer = 1;
        /// 0 when no human is left
        PlayerId _host = 0;
        uint64_t _lobbyRevision = 1;
        /// The public options the lobby shows (SetLobbyOptions)
        Json _lobbyOptions = Json::object();
        bool _public = false;
    };

}  // namespace BECore
