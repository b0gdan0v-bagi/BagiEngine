#include "StatusBarModel.h"

namespace BECore {

    namespace {

        constexpr double kMillisPerSecond = 1000.0;
        constexpr double kTenthsPerMs = 10.0;

    }  // namespace

    FrameLevel ClassifyFrame(double frameMs, double budgetMs) {
        // Written so that NaN falls through to Within
        if (frameMs > budgetMs * kOverBudgetFactor) {
            return FrameLevel::WayOver;
        }
        if (frameMs > budgetMs) {
            return FrameLevel::Over;
        }
        return FrameLevel::Within;
    }

    AllocLevel ClassifyAllocs(uint64_t count) {
        return count == 0 ? AllocLevel::Zero : AllocLevel::Some;
    }

    bool FrameMeter::Feed(const FrameStats& stats) {
        if (stats.frame <= _lastFrame) {
            return false;
        }
        _lastFrame = stats.frame;
        // A held frame costs next to nothing because the game stood still: it would pull the mean of the game's frames down
        if (stats.paused || !(stats.frameMs > 0.0) || !std::isfinite(stats.frameMs)) {
            return true;
        }

        _window[_next] = stats.frameMs;
        _next = (_next + 1) % kMeterWindow;
        _count = std::min(_count + 1, kMeterWindow);

        double sum = 0.0;
        for (size_t i = 0; i < _count; ++i) {
            sum += _window[i];
        }
        const double meanMs = sum / static_cast<double>(_count);

        const double fps = std::round(kMillisPerSecond / meanMs);
        const uint32_t newFps = fps >= static_cast<double>(kMaxMeterFps) ? kMaxMeterFps : static_cast<uint32_t>(fps);
        const double newFrameMs = std::round(meanMs * kTenthsPerMs) / kTenthsPerMs;
        if (newFps != _fps || newFrameMs != _frameMs) {
            _fps = newFps;
            _frameMs = newFrameMs;
            ++_version;
        }
        return true;
    }

    void FormatFps(uint32_t fps, Span<char> buffer) {
        if (!buffer.empty()) {
            std::snprintf(buffer.data(), buffer.size(), "%u fps", fps);
        }
    }

    void FormatFrameMs(double frameMs, Span<char> buffer) {
        if (!buffer.empty()) {
            std::snprintf(buffer.data(), buffer.size(), "%.1f ms", frameMs);
        }
    }

    void FormatAllocs(uint64_t count, Span<char> buffer) {
        if (buffer.empty()) {
            return;
        }
        const unsigned long long shown = count;
        if (count == 1) {
            std::snprintf(buffer.data(), buffer.size(), "%llu alloc", shown);
        } else {
            std::snprintf(buffer.data(), buffer.size(), "%llu allocs", shown);
        }
    }

    void FormatControlStatus(bool listening, uint16_t port, uint32_t clients, Span<char> buffer) {
        if (buffer.empty()) {
            return;
        }
        if (!listening) {
            std::snprintf(buffer.data(), buffer.size(), "off");
        } else if (clients == 1) {
            std::snprintf(buffer.data(), buffer.size(), ":%u  %u client", static_cast<unsigned>(port), clients);
        } else {
            std::snprintf(buffer.data(), buffer.size(), ":%u  %u clients", static_cast<unsigned>(port), clients);
        }
    }

}  // namespace BECore
