#include "RateLimiter.h"

namespace BECore {

    RateLimiter::RateLimiter(int burst, double perSecond) : _capacity(static_cast<double>(burst < 1 ? 1 : burst)), _perSecond(perSecond > 0.0 ? perSecond : 0.0), _tokens(_capacity) {}

    bool RateLimiter::TryTake(RoomTime now) {
        Refill(now);
        if (_tokens < 1.0) {
            return false;
        }
        _tokens -= 1.0;
        return true;
    }

    bool RateLimiter::CanTake(RoomTime now) const {
        return PeekTokens(now) >= 1.0;
    }

    double RateLimiter::PeekTokens(RoomTime now) const {
        RateLimiter copy = *this;
        copy.Refill(now);
        return copy._tokens;
    }

    void RateLimiter::Refill(RoomTime now) {
        if (!_started) {
            _started = true;
            _last = now;
            return;
        }
        if (now <= _last) {
            return;
        }
        const double seconds = std::chrono::duration<double>(now - _last).count();
        _last = now;
        _tokens = std::min(_capacity, _tokens + seconds * _perSecond);
    }

}  // namespace BECore
