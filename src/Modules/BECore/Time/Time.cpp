#include "Time.h"

namespace BECore {

    void Time::Tick(PassKey<Application>) {
        const Clock::time_point now = Clock::now();
        const double elapsed = _lastTick ? std::chrono::duration<double>(now - *_lastTick).count() : 0.0;
        _lastTick = now;
        Advance(elapsed);
    }

    void Time::Advance(double seconds) {
        const double frameTime = std::clamp(seconds, 0.0, kMaxFrameTime);

        _deltaTime = static_cast<float>(frameTime);
        _totalTime += frameTime;
        ++_frameCount;

        _accumulator += frameTime;
        _fixedStepCount = static_cast<uint32_t>(_accumulator / _fixedDeltaTime);
        _accumulator -= static_cast<double>(_fixedStepCount) * _fixedDeltaTime;
    }

    void Time::Reset() {
        _lastTick.reset();
        _accumulator = 0.0;
        _totalTime = 0.0;
        _deltaTime = 0.0f;
        _frameCount = 0;
        _fixedStepCount = 0;
    }

    void Time::SetFixedDeltaTime(double seconds) {
        EXPECT(seconds > 0.0, "Time: the fixed step has to be positive");
        if (seconds > 0.0) {
            _fixedDeltaTime = seconds;
        }
    }

    float Time::GetDeltaTime() const {
        return _deltaTime;
    }

    double Time::GetTotalTime() const {
        return _totalTime;
    }

    uint64_t Time::GetFrameCount() const {
        return _frameCount;
    }

    float Time::GetFixedDeltaTime() const {
        return static_cast<float>(_fixedDeltaTime);
    }

    uint32_t Time::GetFixedStepCount() const {
        return _fixedStepCount;
    }

}  // namespace BECore
