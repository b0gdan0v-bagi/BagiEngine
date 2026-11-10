#pragma once

#include <Games/ISeatedGame.h>

namespace BECore {

    /// Error codes of the room protocol (the `code` of an `error` message). They are part of the protocol: clients match on them.
    /// The errors of the game itself (`illegal_action`, `not_your_turn`, `invalid_seat`) are passed on as the game gave them.
    namespace RoomErrors {
        /// Not JSON, not an object, a missing or wrong-typed field, an unknown message type
        inline constexpr eastl::string_view kBadMessage = "bad_message";
        /// `v` is not the version of the protocol the server speaks
        inline constexpr eastl::string_view kUnsupportedVersion = "unsupported_version";
        inline constexpr eastl::string_view kMessageTooLarge = "message_too_large";
        inline constexpr eastl::string_view kRateLimited = "rate_limited";
        inline constexpr eastl::string_view kUnknownGame = "unknown_game";
        /// No room has this code (also a code that could not be a code)
        inline constexpr eastl::string_view kUnknownRoom = "unknown_room";
        inline constexpr eastl::string_view kRoomFull = "room_full";
        inline constexpr eastl::string_view kNotInRoom = "not_in_room";
        inline constexpr eastl::string_view kAlreadyInRoom = "already_in_room";
        inline constexpr eastl::string_view kNotHost = "not_host";
        /// The room has started: only a member can return to it, by token
        inline constexpr eastl::string_view kGameStarted = "game_started";
        inline constexpr eastl::string_view kNotStarted = "not_started";
        inline constexpr eastl::string_view kSeatTaken = "seat_taken";
        inline constexpr eastl::string_view kInvalidSeat = SeatedGameErrors::kInvalidSeat;
        /// `action.version` is not the version of the game now; the current `view` is sent again
        inline constexpr eastl::string_view kStaleAction = "stale_action";
        inline constexpr eastl::string_view kIllegalAction = GameControlErrors::kIllegalAction;
        inline constexpr eastl::string_view kNotYourTurn = SeatedGameErrors::kNotYourTurn;
        inline constexpr eastl::string_view kBadName = "bad_name";
        /// The token is not one a room hands out, or no member of the room has it
        inline constexpr eastl::string_view kBadToken = "bad_token";
        inline constexpr eastl::string_view kTooManyRooms = "too_many_rooms";
        inline constexpr eastl::string_view kInternalError = "internal_error";
    }  // namespace RoomErrors

}  // namespace BECore
