#pragma once

#include <Rooms/RoomLimits.h>
#include <Rooms/RoomTypes.h>

namespace BECore {

    /**
     * @brief A token bucket over injected time: @c burst messages at once, then @c perSecond more each second.
     *
     * One per connection. It starts full at the first call, so a connection may send its burst right away. Time is passed in
     * (RoomTime), never read; a time that goes back counts as no time passing. Not thread-safe (the hub's thread only).
     */
    class RateLimiter {
    public:
        /// @param burst The size of the bucket and how full it starts; below 1 is 1.
        /// @param perSecond Tokens added per second; zero, negative or not a number adds none.
        explicit RateLimiter(int burst = RoomLimits::kRateBurst, double perSecond = RoomLimits::kRatePerSecond);

        /// Takes one token if there is one. False means: refuse this message.
        [[nodiscard]] bool TryTake(RoomTime now);

        /// Takes one token if there is one and does not say whether it did (for a charge that has no refusal to give)
        void Spend(RoomTime now);

        /// Whether TryTake would succeed at @p now, taking nothing (for a check that has to come before the take)
        [[nodiscard]] bool CanTake(RoomTime now) const;

        /// The tokens the bucket holds at @p now (without taking any), for tests and logs
        [[nodiscard]] double PeekTokens(RoomTime now) const;

    private:
        void Refill(RoomTime now);

        double _capacity = 1.0;
        double _perSecond = 0.0;
        double _tokens = 1.0;
        RoomTime _last{};
        bool _started = false;
    };

}  // namespace BECore
