#pragma once

#include <Rooms/RoomTypes.h>

namespace BECore {

    /// The limits of a room server. They are the defaults of RoomHubSettings; a test overrides them there.
    namespace RoomLimits {
        /// A client message longer than this is refused before it is parsed. It also bounds the nesting of a JSON bomb.
        inline constexpr size_t kMaxMessageBytes = 16 * 1024;

        /// The deepest nesting of arrays and objects a client's message may have (`options` and `action` are game data, but no game's is deep). Copying and dumping a
        /// value recurses once per level, and a message of kMaxMessageBytes could otherwise be 8000 levels deep, on a reader thread with a small stack
        inline constexpr size_t kMaxClientJsonDepth = 32;

        /// A message from the server that a client reads: its views are small, but a game's view may grow (the opposite direction is
        /// bounded by kMaxMessageBytes, which a client's `action` has to fit)
        inline constexpr size_t kMaxServerMessageBytes = 1024 * 1024;

        /// The deepest nesting of arrays and objects a client accepts in a message from the server. The parser reads a deeper one
        /// without recursion, but copying the game's `view` or `actions` out of it recurses once per level and a bomb in 1 MiB
        /// would overflow the stack
        inline constexpr size_t kMaxServerJsonDepth = 128;

        /// The most seats a `lobby` message may list when a client reads it (a bound on the work, not a limit of any game)
        inline constexpr size_t kMaxLobbySeats = 64;

        /// A player name: 1 to 24 UTF-8 bytes (after trimming) of printable text
        inline constexpr size_t kMinNameBytes = 1;
        inline constexpr size_t kMaxNameBytes = 24;

        /// The name of a game in `create_room`
        inline constexpr size_t kMaxGameNameBytes = 64;

        /// A room code: 5 characters of this alphabet (32 letters and digits without I, O, 0 and 1), upper case
        inline constexpr size_t kRoomCodeLength = 5;
        inline constexpr eastl::string_view kRoomCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

        /// A reconnect token: 128 random bits as lower-case hexadecimal
        inline constexpr size_t kTokenLength = 32;

        inline constexpr size_t kMaxRooms = 64;
        inline constexpr size_t kMaxConnections = 512;

        /// The token bucket of one connection: this many messages at once, then this many per second
        inline constexpr int kRateBurst = 20;
        inline constexpr double kRatePerSecond = 10.0;

        /// A connection is closed after this many `rate_limited` answers in a row
        inline constexpr int kMaxRateLimitedInRow = 5;

        /// A connection is closed after this many failed `join_room` in a row (an unknown code or a bad token: guessing)
        inline constexpr int kMaxFailedJoinsInRow = 5;

        /// A connection that is in no room is closed after this long without a message
        inline constexpr std::chrono::seconds kIdleTimeout{30};

        /// A player whose connection dropped keeps the seat for this long; then a computer plays it until the player returns
        inline constexpr std::chrono::seconds kReconnectGrace{30};

        /// A room whose humans are all gone or away for this long is closed
        inline constexpr std::chrono::minutes kAbandonedTimeout{10};

        /// The pause between two moves of computers in one room
        inline constexpr std::chrono::milliseconds kComputerDelay{800};
    }  // namespace RoomLimits

    /// The times a room works with (RoomLimits by default)
    struct RoomTimings {
        /// A player whose connection dropped keeps the seat this long; then a computer plays it until the player returns
        std::chrono::milliseconds reconnectGrace = RoomLimits::kReconnectGrace;
        /// A room whose humans are all away for this long is closed
        std::chrono::milliseconds abandonedTimeout = RoomLimits::kAbandonedTimeout;
        /// The pause between two moves of computers
        std::chrono::milliseconds computerDelay = RoomLimits::kComputerDelay;
    };

    /// The limits one hub runs with (RoomLimits by default)
    struct RoomHubSettings {
        size_t maxMessageBytes = RoomLimits::kMaxMessageBytes;
        size_t maxRooms = RoomLimits::kMaxRooms;
        size_t maxConnections = RoomLimits::kMaxConnections;
        int rateBurst = RoomLimits::kRateBurst;
        double ratePerSecond = RoomLimits::kRatePerSecond;
        int maxRateLimitedInRow = RoomLimits::kMaxRateLimitedInRow;
        int maxFailedJoinsInRow = RoomLimits::kMaxFailedJoinsInRow;
        std::chrono::milliseconds idleTimeout = RoomLimits::kIdleTimeout;
        /// The times of every room of the hub
        RoomTimings timings;
    };

}  // namespace BECore
