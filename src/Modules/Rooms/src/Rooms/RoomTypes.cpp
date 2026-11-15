#include "RoomTypes.h"

#include <random>

namespace BECore {

    RandomFn MakeSecureRandom() {
        // std::random_device has neither a copy nor a move constructor, so the capture holds it through a pointer
        return [device = MakeUnique<std::random_device>()]() -> uint64_t {
            const uint64_t high = (*device)();
            const uint64_t low = (*device)();
            return (high << 32) ^ low;
        };
    }

    StringView GetPhaseName(RoomPhase phase) {
        switch (phase) {
            case RoomPhase::Lobby:
                return "lobby";
            case RoomPhase::Playing:
                return "playing";
            case RoomPhase::Finished:
                return "finished";
        }
        return "lobby";
    }

    StringView GetSeatKindName(SeatKind kind) {
        switch (kind) {
            case SeatKind::Human:
                return "human";
            case SeatKind::Computer:
                return "computer";
            case SeatKind::Empty:
                return "empty";
        }
        return "empty";
    }

}  // namespace BECore
