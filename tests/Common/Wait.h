#pragma once

#include <chrono>
#include <thread>

namespace BECore::Tests {

    /// The limit of a poll for something that must happen. A passing test never waits it out, so it is generous: ctest and the parallel workers
    /// load every core of the machine, and a short limit turns that load into a failure
    inline constexpr std::chrono::seconds kWaitLimit{30};

    /// Asks @p done every millisecond until it is true or @p limit has passed; whether it became true. A fixed sleep before a check is either
    /// flaky on a loaded machine or needlessly slow, a poll is neither
    template <typename TPredicate>
    bool WaitUntil(TPredicate&& done, std::chrono::milliseconds limit = kWaitLimit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

}  // namespace BECore::Tests
