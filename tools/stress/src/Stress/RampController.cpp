#include "RampController.h"

#include <EASTL/sort.h>

namespace BECore::Stress {

    namespace {

        /// Room for the ramp line (under 300 characters), so that writing it does not grow the string
        constexpr size_t kLineReserve = 384;

        /// The count after @p count on the way up: x kRampGrowth, at least +1, never above kMaxCount
        uint32_t Grown(uint32_t count) {
            const auto scaled = static_cast<uint64_t>(static_cast<double>(count) * kRampGrowth);
            const uint64_t next = std::max<uint64_t>(scaled, uint64_t{count} + 1);
            return static_cast<uint32_t>(std::min<uint64_t>(next, kMaxCount));
        }

        /// The widest bracket the bisection leaves: kRampPrecision of @p lo, at least 1
        uint32_t Resolution(uint32_t lo) {
            return std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<double>(lo) * kRampPrecision));
        }

        void AddBool(eastl::string& line, eastl::string_view key, bool value) {
            line += '"';
            line.append(key.data(), key.size());
            line += value ? "\":true," : "\":false,";
        }

        void AddInteger(eastl::string& line, eastl::string_view key, uint32_t value) {
            line += Format("\"{}\":{},", key, value);
        }

        void AddText(eastl::string& line, eastl::string_view key, eastl::string_view value) {
            line += Format("\"{}\":\"{}\",", key, value);
        }

    }  // namespace

    RampController::RampController(StressMeasure measure, uint32_t maxFrames) : _measure(measure), _maxFrames(maxFrames) {}

    RampNext RampController::OnFrame(double measureMs) {
        if (_phase == Phase::Done) {
            return {_count, true};
        }
        ++_result.frames;
        if (_result.frames > _maxFrames) {
            _result.aborted = true;
            Finish(_lo, false);
            return {_count, true};
        }
        if (_settleLeft > 0) {
            --_settleLeft;
            return {_count, false};
        }

        _window[_measured++] = measureMs;
        if (_measured < kMeasureFrames) {
            return {_count, false};
        }

        // The step is over: its verdict is the percentile of the measured frames, on a sorted copy (no allocation)
        ++_result.steps;
        eastl::array<double, kMeasureFrames> sorted = _window;
        eastl::sort(sorted.begin(), sorted.end());
        OnStepEnd(NearestRank(sorted, kRampPercentile) <= kRampBudgetMs);
        return {_count, _phase == Phase::Done};
    }

    void RampController::OnStepEnd(bool passed) {
        switch (_phase) {
            case Phase::Growing:
                if (passed) {
                    _lo = _count;
                    if (_count == kMaxCount) {
                        _result.capped = true;
                        Finish(_count, true);
                    } else {
                        Show(Grown(_count));
                    }
                } else if (_lo == 0) {
                    Finish(0, true);  // even the start does not fit
                } else {
                    _hi = _count;
                    _phase = Phase::Bisecting;
                    Narrow();
                }
                break;
            case Phase::Bisecting:
                (passed ? _lo : _hi) = _count;
                Narrow();
                break;
            case Phase::Verifying:
                if (passed) {
                    Finish(_count, true);
                } else if (_stepDowns >= kMaxStepDowns) {
                    Finish(_count, false);
                } else if (_count <= _stepDown) {
                    Finish(0, false);  // stepped down to nothing: no count was verified
                } else {
                    ++_stepDowns;
                    Show(_count - _stepDown);
                }
                break;
            case Phase::Done:
                break;
        }
    }

    void RampController::Narrow() {
        if (_hi - _lo <= Resolution(_lo)) {
            _phase = Phase::Verifying;
            _stepDown = Resolution(_lo);
            Show(_lo);
        } else {
            Show(_lo + (_hi - _lo) / 2);
        }
    }

    void RampController::Show(uint32_t count) {
        _count = count;
        _settleLeft = kSettleFrames;
        _measured = 0;
    }

    void RampController::Finish(uint32_t count, bool verified) {
        _phase = Phase::Done;
        _result.objectsAt60fps = count;
        _result.verified = verified;
    }

    eastl::string FormatRampLine(const RampResult& result, StressMeasure measure, const RampRunContext& context) {
        eastl::string line;
        line.reserve(kLineReserve);
        // Sorted by key, byte order, as the fixed run's line is
        AddBool(line, "aborted", result.aborted);
        AddBool(line, "allocTracking", context.allocTracking);
        AddInteger(line, "atlases", context.atlases);
        line += Format("\"budgetMs\":{},", FormatNumber(kRampBudgetMs));
        AddBool(line, "capped", result.capped);
        AddInteger(line, "frames", result.frames);
        AddBool(line, "interleave", context.interleave);
        AddBool(line, "manualClock", context.manualClock);
        AddText(line, "measure", ToFlagValue(measure));
        AddInteger(line, "measureFrames", kMeasureFrames);
        AddText(line, "mode", "ramp");
        AddInteger(line, "objectsAt60fps", result.objectsAt60fps);
        AddText(line, "scene", ToFlagValue(context.scene));
        AddInteger(line, "settleFrames", kSettleFrames);
        AddInteger(line, "steps", result.steps);
        line += Format("\"verified\":{}", result.verified ? "true" : "false");
        line.insert(size_t{0}, 1, '{');
        line += '}';
        return line;
    }

}  // namespace BECore::Stress
