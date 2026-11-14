#pragma once

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

        [[nodiscard]] std::function<RoomTime()> AsFunction() {
            return [state = _state] { return state->Now(); };
        }

    private:
        struct State {
            const RoomTime start = std::chrono::steady_clock::now();
            std::atomic<int64_t> elapsedMs{0};

            RoomTime Now() const {
                return start + std::chrono::milliseconds(elapsedMs.load());
            }
        };

        SharedPtr<State> _state = MakeShared<State>();
    };

}  // namespace BECore::Tests
