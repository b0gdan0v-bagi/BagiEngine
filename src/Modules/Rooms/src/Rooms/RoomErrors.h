#pragma once

#include <Games/ISeatedGame.h>

namespace BECore {

    /// Error codes of the room protocol (the `code` of an `error` message). They are part of the protocol: clients match on them.
    /// The errors of the game itself (`illegal_action`, `not_your_turn`, `invalid_seat`) are passed on as the game gave them.
    namespace RoomErrors {
        /// Not JSON, not an object, a missing or wrong-typed field, an unknown message type
        inline constexpr StringView kBadMessage = "bad_message";
        /// `v` is not the version of the protocol the server speaks
        inline constexpr StringView kUnsupportedVersion = "unsupported_version";
        inline constexpr StringView kMessageTooLarge = "message_too_large";
        inline constexpr StringView kRateLimited = "rate_limited";
        inline constexpr StringView kUnknownGame = "unknown_game";
        /// No room has this code (also a code that could not be a code)
        inline constexpr StringView kUnknownRoom = "unknown_room";
        inline constexpr StringView kRoomFull = "room_full";
        inline constexpr StringView kNotInRoom = "not_in_room";
        inline constexpr StringView kAlreadyInRoom = "already_in_room";
        inline constexpr StringView kNotHost = "not_host";
        /// The room has started: only a member can return to it, by token
        inline constexpr StringView kGameStarted = "game_started";
        inline constexpr StringView kNotStarted = "not_started";
        inline constexpr StringView kSeatTaken = "seat_taken";
        inline constexpr StringView kInvalidSeat = SeatedGameErrors::kInvalidSeat;
        /// `action.version` is not the version of the game now; the current `view` is sent again
        inline constexpr StringView kStaleAction = "stale_action";
        inline constexpr StringView kIllegalAction = GameControlErrors::kIllegalAction;
        inline constexpr StringView kNotYourTurn = SeatedGameErrors::kNotYourTurn;
        inline constexpr StringView kBadName = "bad_name";
        /// The token is not one a room hands out, or no member of the room has it
        inline constexpr StringView kBadToken = "bad_token";
        inline constexpr StringView kTooManyRooms = "too_many_rooms";
        inline constexpr StringView kInternalError = "internal_error";
    }  // namespace RoomErrors

}  // namespace BECore
