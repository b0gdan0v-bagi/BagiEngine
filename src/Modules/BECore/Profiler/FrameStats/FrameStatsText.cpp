#include "FrameStatsText.h"

namespace BECore {

    namespace {

        constexpr double kKilobyte = 1024.0;
        constexpr double kMillisecondsPerSecond = 1000.0;

    }  // namespace

    eastl::string FormatBytes(uint64_t bytes) {
        const auto value = static_cast<double>(bytes);
        if (value < kKilobyte) {
            return Format("{} B", bytes);
        }
        if (value < kKilobyte * kKilobyte) {
            return Format("{:.1f} KB", value / kKilobyte);
        }
        return Format("{:.1f} MB", value / (kKilobyte * kKilobyte));
    }

    eastl::vector<eastl::string> FormatOverlayLines(const FrameStats& stats, const AllocTotals& totals, bool allocTracking) {
        eastl::vector<eastl::string> lines;
        const double fps = stats.frameMs > 0.0 ? kMillisecondsPerSecond / stats.frameMs : 0.0;
        lines.push_back(Format("Frame {}  {:.2f} ms ({:.0f} fps)  CPU {:.2f} ms{}", stats.frame, stats.frameMs, fps, stats.cpuMs, stats.paused ? "  paused" : ""));
        lines.push_back(Format("Update {:.2f} (scene {:.2f})  Render {:.2f} (scene {:.2f})  Present {:.2f} ms", stats.GetStageMs(FrameStage::Update), stats.GetStageMs(FrameStage::SceneUpdate),
                               stats.GetStageMs(FrameStage::Render), stats.GetStageMs(FrameStage::SceneDraw), stats.GetStageMs(FrameStage::Present)));
        lines.push_back(Format("Draw calls {}  Triangles {}  Texture switches {}", stats.draws.drawCalls, stats.draws.triangles, stats.draws.textureSwitches));
        if (allocTracking) {
            lines.push_back(
                Format("Allocations {} ({}) this frame  Live {}  Peak {}", stats.allocCount, FormatBytes(stats.allocBytes), FormatBytes(totals.liveBytes), FormatBytes(totals.peakLiveBytes)));
        } else {
            lines.push_back(eastl::string("Allocations not counted (ENGINE_PROFILER_ALLOC=OFF)"));
        }
        for (const FrameCounter& counter : stats.counters) {
            lines.push_back(Format("{} {}", counter.name.ToStringView(), counter.value));
        }
        return lines;
    }

}  // namespace BECore
