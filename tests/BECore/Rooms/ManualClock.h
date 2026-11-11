#pragma once

#include <Rooms/RoomTypes.h>
#include <atomic>

namespace BECore::Tests {

    /// The time the hub and the liveness check see, moved by the test instead of waited for
    class ManualClock {
    public:
        RoomTime Now() const {
            return _start + std::chrono::milliseconds(_elapsedMs.load());
        }

        void Advance(std::chrono::milliseconds step) {
            _elapsedMs += step.count();
        }

        [[nodiscard]] std::function<RoomTime()> AsFunction() {
            return [this] { return Now(); };
        }

    private:
        const RoomTime _start = std::chrono::steady_clock::now();
        std::atomic<int64_t> _elapsedMs{0};
    };

}  // namespace BECore::Tests
