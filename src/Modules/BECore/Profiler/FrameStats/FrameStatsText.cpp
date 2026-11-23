#include "FrameStatsText.h"

#include <bit>

namespace BECore {

    namespace {

        constexpr double kKilobyte = 1024.0;
        constexpr double kMillisecondsPerSecond = 1000.0;

        /// Whether @p a is listed before @p b: larger self time first; a tie by identity and, for two sites of one name, by address
        bool ListedBefore(const ZoneStatRow& a, const ZoneStatRow& b) {
            if (a.selfMs != b.selfMs) {
                return a.selfMs > b.selfMs;
            }
            if (const int byIdentity = CompareZoneStatIdentity(a, b); byIdentity != 0) {
                return byIdentity < 0;
            }
            return std::bit_cast<uintptr_t>(a.site) < std::bit_cast<uintptr_t>(b.site);
        }

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

    void FormatTopZoneLinesTo(const Vector<ZoneStatRow>& rows, uint32_t windowFrames, size_t count, StringView skipName, FunctionRef<void(StringView)> emit) {
        if (windowFrames == 0) {
            return;
        }
        char buffer[kOverlayLineCapacity];
        const double frames = static_cast<double>(windowFrames);
        const ZoneStatRow* last = nullptr;
        for (size_t line = 0; line < count; ++line) {
            // The best row that is listed after the previous line's
            const ZoneStatRow* best = nullptr;
            for (const ZoneStatRow& row : rows) {
                if (StringView(row.site->name) == skipName || (last != nullptr && !ListedBefore(*last, row))) {
                    continue;
                }
                if (best == nullptr || ListedBefore(row, *best)) {
                    best = &row;
                }
            }
            if (best == nullptr) {
                return;
            }
            const FormatResult written = FormatToN(buffer, kOverlayLineCapacity, "{}  {:.2f} ms  {:.1f}x", best->site->name, best->selfMs / frames, static_cast<double>(best->calls) / frames);
            emit(StringView(buffer, eastl::min(written.size, kOverlayLineCapacity)));
            last = best;
        }
    }

    Vector<String> FormatOverlayLines(const FrameStats& stats, const AllocTotals& totals, bool allocTracking) {
        Vector<String> lines;
        FormatOverlayLinesTo(stats, totals, allocTracking, [&lines](StringView line) { lines.push_back(String(line.data(), line.size())); });
        return lines;
    }

}  // namespace BECore
