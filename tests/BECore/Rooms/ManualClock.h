#pragma once

#include <Common/SharedPtr.h>
#include <Rooms/RoomTypes.h>
#include <memory>

namespace BECore::Tests {

    /// The time the hub and the liveness check see, moved by the test instead of waited for.
    /// The state is shared with every function made by AsFunction(): the hub thread of a server
    /// keeps calling it until the fixture stops the server, which may be after the test body (and
    /// the clock local in it) is gone
    class ManualClock {
    public:
        RoomTime Now() const {
            return _state->Now();
        }

        void Advance(std::chrono::milliseconds step) {
            _state->elapsedMs += step.count();
        }

        /// How many times anyone has read the time, so a test can wait until the hub thread has looked at a value
        [[nodiscard]] uint64_t Reads() const {
            return _state->reads.load();
        }

        [[nodiscard]] MoveOnlyFunction<RoomTime()> AsFunction() {
            return [state = _state] { return state->Now(); };
        }

    private:
        struct State {
            const RoomTime start = std::chrono::steady_clock::now();
            std::atomic<int64_t> elapsedMs{0};
            mutable std::atomic<uint64_t> reads{0};

            RoomTime Now() const {
                reads.fetch_add(1);
                return start + std::chrono::milliseconds(elapsedMs.load());
            }
        };

        SharedPtr<State> _state = MakeShared<State>();
    };

}  // namespace BECore::Tests
