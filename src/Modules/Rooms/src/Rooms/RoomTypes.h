#pragma once

#include <chrono>

namespace BECore {

    /// A transport connection (a WebSocket), numbered by the transport; the hub never reuses a number while it is alive
    using ConnectionId = uint64_t;

    /// A member of a room, numbered by the room from 1; never reused inside the room
    using PlayerId = uint32_t;

    /// Time as the room logic sees it. The caller passes it in (the hub never reads a clock), so tests never sleep.
    using RoomTime = std::chrono::steady_clock::time_point;

    /// Source of random 64-bit words, injected so that a test is deterministic
    using RandomFn = std::function<uint64_t()>;

    /// Where a room is in its life
    CORE_ENUM(RoomPhase, uint8_t, Lobby, Playing, Finished)

    /// Who sits on a seat
    CORE_ENUM(SeatKind, uint8_t, Human, Computer, Empty)

    /// Random words from the operating system (std::random_device). For room codes and tokens, which must not be guessable.
    [[nodiscard]] RandomFn MakeSecureRandom();

    /// The wire name of a phase: `lobby`, `playing` or `finished`
    [[nodiscard]] eastl::string_view GetPhaseName(RoomPhase phase);

    /// The wire name of a seat kind: `human`, `computer` or `empty`
    [[nodiscard]] eastl::string_view GetSeatKindName(SeatKind kind);

}  // namespace BECore
