#include "FrameStatsText.h"

namespace BECore {

    namespace {

        constexpr double kKilobyte = 1024.0;
        constexpr double kMillisecondsPerSecond = 1000.0;

    }  // namespace

    String FormatBytes(uint64_t bytes) {
        const auto value = static_cast<double>(bytes);
        if (value < kKilobyte) {
            return Format("{} B", bytes);
        }
        if (value < kKilobyte * kKilobyte) {
            return Format("{:.1f} KB", value / kKilobyte);
        }
        return Format("{:.1f} MB", value / (kKilobyte * kKilobyte));
    }

    void FormatOverlayLinesTo(const FrameStats& stats, const AllocTotals& totals, bool allocTracking, FunctionRef<void(StringView)> emit) {
        char buffer[kOverlayLineCapacity];
        const auto give = [&](const FormatResult& written) { emit(StringView(buffer, eastl::min(written.size, kOverlayLineCapacity))); };
        const double fps = stats.frameMs > 0.0 ? kMillisecondsPerSecond / stats.frameMs : 0.0;
        give(FormatToN(buffer, kOverlayLineCapacity, "Frame {}  {:.2f} ms ({:.0f} fps)  CPU {:.2f} ms{}", stats.frame, stats.frameMs, fps, stats.cpuMs, stats.paused ? "  paused" : ""));
        give(FormatToN(buffer, kOverlayLineCapacity, "Update {:.2f} (scene {:.2f})  Render {:.2f} (scene {:.2f})  Present {:.2f}  Wait {:.2f} ms", stats.GetStageMs(FrameStage::Update),
                       stats.GetStageMs(FrameStage::SceneUpdate), stats.GetStageMs(FrameStage::Render), stats.GetStageMs(FrameStage::SceneDraw), stats.GetStageMs(FrameStage::Present),
                       stats.GetStageMs(FrameStage::Wait)));
        give(FormatToN(buffer, kOverlayLineCapacity, "Draw calls {}  Triangles {}  Texture switches {}", stats.draws.drawCalls, stats.draws.triangles, stats.draws.textureSwitches));
        if (allocTracking) {
            give(FormatToN(buffer, kOverlayLineCapacity, "Allocations {} ({}) this frame  Live {}  Peak {}", stats.allocCount, FormatBytes(stats.allocBytes), FormatBytes(totals.liveBytes),
                           FormatBytes(totals.peakLiveBytes)));
        } else {
            emit("Allocations not counted (ENGINE_PROFILER_ALLOC=OFF)");
        }
        for (const FrameCounter& counter : stats.counters) {
            give(FormatToN(buffer, kOverlayLineCapacity, "{} {}", counter.name.ToStringView(), counter.value));
        }
    }

    Vector<String> FormatOverlayLines(const FrameStats& stats, const AllocTotals& totals, bool allocTracking) {
        Vector<String> lines;
        FormatOverlayLinesTo(stats, totals, allocTracking, [&lines](StringView line) { lines.push_back(String(line.data(), line.size())); });
        return lines;
    }

}  // namespace BECore
