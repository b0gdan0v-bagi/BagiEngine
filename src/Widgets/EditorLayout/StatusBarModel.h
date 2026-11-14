#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>

namespace BECore {

    /// The frame budget of the status bar: 60 fps, fixed (the Profiler panel lets the user pick one, the bar does not)
    inline constexpr double kFrameBudgetMs = 1000.0 / 60.0;
    /// A frame over this many budgets is the worst level (the rule of the Profiler panel's bars)
    inline constexpr double kOverBudgetFactor = 2.0;
    /// Frames the FrameMeter averages
    inline constexpr size_t kMeterWindow = 30;
    /// The largest FPS the meter reports, so the text stays short when the mean frame time is close to zero
    inline constexpr uint32_t kMaxMeterFps = 9999;

    /// How a frame time compares with the budget; the colours (success, warning, error) are the widget's business
    CORE_ENUM(FrameLevel, uint8_t, Within, Over, WayOver)

    /// How many allocations the last frame made
    CORE_ENUM(AllocLevel, uint8_t, Zero, Some)

    /// Within when @p frameMs <= @p budgetMs, Over up to twice the budget (inclusive), WayOver above. Zero, negative and NaN are Within.
    [[nodiscard]] FrameLevel ClassifyFrame(double frameMs, double budgetMs);

    [[nodiscard]] AllocLevel ClassifyAllocs(uint64_t count);

    /**
     * @brief The frame time and FPS of the status bar: the mean of the last kMeterWindow frames, rounded.
     *
     * Fed once per frame with the last finished frame of the FrameStatsCollector. A frame it has seen already (the idle pass of the
     * manual clock finishes none) is ignored, and so are a frame with a zero, negative or non-finite time and a paused (held) frame, which is not a game frame (the numbers stand as they were while
     * the game is paused). FPS is `1000 / mean` rounded to an integer, the frame time is rounded to one decimal; Version() moves only when one of these two changes, so a caller makes its text again
     * only then. Plain data without an ImGui context.
     */
    class FrameMeter {
    public:
        /// Takes @p stats into the window when its frame is newer than the last seen one; true when it did
        bool Feed(const FrameStats& stats);

        /// 0 before a usable frame
        [[nodiscard]] uint32_t GetFps() const {
            return _fps;
        }
        /// The mean frame time rounded to one decimal; 0 before a usable frame
        [[nodiscard]] double GetFrameMs() const {
            return _frameMs;
        }
        /// The level of the shown frame time against kFrameBudgetMs
        [[nodiscard]] FrameLevel GetLevel() const {
            return ClassifyFrame(_frameMs, kFrameBudgetMs);
        }
        /// Moves when GetFps() or GetFrameMs() changes
        [[nodiscard]] uint32_t Version() const {
            return _version;
        }

    private:
        Array<double, kMeterWindow> _window{};
        size_t _count = 0;  // filled slots, up to kMeterWindow
        size_t _next = 0;   // the slot the next frame goes to
        uint64_t _lastFrame = 0;
        uint32_t _fps = 0;
        double _frameMs = 0.0;
        uint32_t _version = 0;
    };

    // The formatters write NUL-terminated text into the caller's buffer and cut it when it is too small; none allocates.

    /// "60 fps"
    void FormatFps(uint32_t fps, Span<char> buffer);
    /// "16.7 ms"
    void FormatFrameMs(double frameMs, Span<char> buffer);
    /// "0 allocs", "1 alloc", "12 allocs"
    void FormatAllocs(uint64_t count, Span<char> buffer);
    /// "off" when not listening, otherwise ":<port>  <n> client(s)"
    void FormatControlStatus(bool listening, uint16_t port, uint32_t clients, Span<char> buffer);

}  // namespace BECore
