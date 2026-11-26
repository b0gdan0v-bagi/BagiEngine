#include "StressRecorder.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore::Stress {

    namespace {

        constexpr double kRankNudge = 1e-9;
        constexpr double kMedian = 0.5;
        constexpr double kTail = 0.99;
        constexpr int kDecimals = 4;
        /// Room for the fixed line with large numbers in every key, so that writing it does not grow the string
        constexpr size_t kLineReserve = 1536;

        /// The sorted values of one field of the samples
        template <typename Field>
        Vector<double> SortedOf(const Vector<StressSample>& samples, Field field) {
            Vector<double> values;
            values.reserve(samples.size());
            for (const StressSample& sample : samples) {
                values.push_back(sample.*field);
            }
            eastl::sort(values.begin(), values.end());
            return values;
        }

        template <typename Field>
        double MedianOf(const Vector<StressSample>& samples, Field field) {
            return NearestRank(SortedOf(samples, field), kMedian);
        }

        /// The tail of @p sorted that holds a GPU time: the frames without one are the zeros in front
        Span<const double> WithGpuTime(const Vector<double>& sorted) {
            const auto first = eastl::upper_bound(sorted.begin(), sorted.end(), 0.0);
            return Span<const double>(sorted.data(), sorted.size()).subspan(static_cast<size_t>(first - sorted.begin()));
        }

        /// A `"key":value` member (a number written by FormatNumber); the keys are added in sorted order by the caller
        void AddMember(String& line, StringView key, const String& value) {
            if (!line.empty()) {
                line += ',';
            }
            line += '"';
            line.append(key.data(), key.size());
            line += "\":";
            line += value;
        }

        void AddNumber(String& line, StringView key, double value) {
            AddMember(line, key, FormatNumber(value));
        }

        void AddInteger(String& line, StringView key, uint32_t value) {
            AddMember(line, key, Format("{}", value));
        }

        void AddBool(String& line, StringView key, bool value) {
            AddMember(line, key, value ? "true" : "false");
        }

        void AddText(String& line, StringView key, StringView value) {
            String quoted("\"");
            quoted.append(value.data(), value.size());
            quoted += '"';
            AddMember(line, key, quoted);
        }

    }  // namespace

    StressSample SampleOf(const FrameStats& stats) {
        StressSample sample;
        sample.cpuMs = stats.cpuMs;
        sample.updateMs = stats.GetStageMs(FrameStage::Update);
        sample.submitMs = stats.GetStageMs(FrameStage::Submit);
        sample.drawMs = std::max(0.0, stats.GetStageMs(FrameStage::SceneDraw) - sample.submitMs);
        sample.presentMs = stats.GetStageMs(FrameStage::Present);
        sample.waitMs = stats.GetStageMs(FrameStage::Wait);
        sample.gpuMs = stats.gpuMs;
        sample.workMs = std::max(0.0, stats.cpuMs - sample.presentMs - sample.waitMs);
        sample.frameMs = stats.frameMs;
        sample.drawCalls = stats.draws.drawCalls;
        sample.primitives = stats.draws.primitives;
        sample.triangles = stats.draws.triangles;
        sample.textureSwitches = stats.draws.textureSwitches;
        sample.allocCount = stats.allocCount;
        sample.allocBytes = stats.allocBytes;
        return sample;
    }

    double NearestRank(Span<const double> sorted, double fraction) {
        if (sorted.empty()) {
            return 0.0;
        }
        const double rank = std::ceil(fraction * static_cast<double>(sorted.size()) - kRankNudge);
        const size_t index = rank < 1.0 ? 0 : std::min(sorted.size(), static_cast<size_t>(rank)) - 1;
        return sorted[index];
    }

    void StageSum::Add(const StageTimes& frame) {
        _sum.updateMs += frame.updateMs;
        _sum.drawMs += frame.drawMs;
        _sum.submitMs += frame.submitMs;
        _sum.presentMs += frame.presentMs;
        _sum.waitMs += frame.waitMs;
        ++_frames;
        if (frame.gpuMs > 0.0) {
            _sum.gpuMs += frame.gpuMs;
            ++_gpuFrames;
        }
    }

    StageTimes StageSum::Mean() const {
        StageTimes mean;
        if (_frames > 0) {
            const double n = static_cast<double>(_frames);
            mean.updateMs = _sum.updateMs / n;
            mean.drawMs = _sum.drawMs / n;
            mean.submitMs = _sum.submitMs / n;
            mean.presentMs = _sum.presentMs / n;
            mean.waitMs = _sum.waitMs / n;
        }
        if (_gpuFrames > 0) {
            mean.gpuMs = _sum.gpuMs / static_cast<double>(_gpuFrames);
        }
        return mean;
    }

    void StressRecorder::Reserve(uint32_t frames) {
        _samples.clear();
        _samples.reserve(frames);
        _limit = frames;
    }

    bool StressRecorder::Add(const StressSample& sample) {
        if (_samples.size() >= _limit) {
            return false;
        }
        _samples.push_back(sample);
        return true;
    }

    StressSummary StressRecorder::Summarize() const {
        StressSummary summary;
        summary.frames = static_cast<uint32_t>(_samples.size());
        if (_samples.empty()) {
            return summary;
        }
        const auto frameMs = SortedOf(_samples, &StressSample::frameMs);
        const auto cpuMs = SortedOf(_samples, &StressSample::cpuMs);
        const auto workMs = SortedOf(_samples, &StressSample::workMs);
        summary.frameMsP50 = NearestRank(frameMs, kMedian);
        summary.frameMsP99 = NearestRank(frameMs, kTail);
        summary.cpuMsP50 = NearestRank(cpuMs, kMedian);
        summary.cpuMsP99 = NearestRank(cpuMs, kTail);
        summary.workMsP50 = NearestRank(workMs, kMedian);
        summary.workMsP99 = NearestRank(workMs, kTail);

        summary.stagesP50.updateMs = MedianOf(_samples, &StressSample::updateMs);
        summary.stagesP50.drawMs = MedianOf(_samples, &StressSample::drawMs);
        summary.stagesP50.submitMs = MedianOf(_samples, &StressSample::submitMs);
        summary.stagesP50.presentMs = MedianOf(_samples, &StressSample::presentMs);
        summary.stagesP50.waitMs = MedianOf(_samples, &StressSample::waitMs);
        const auto gpuMs = SortedOf(_samples, &StressSample::gpuMs);
        summary.stagesP50.gpuMs = NearestRank(WithGpuTime(gpuMs), kMedian);
        summary.gpuMsP99 = NearestRank(WithGpuTime(gpuMs), kTail);

        double drawCalls = 0.0;
        double primitives = 0.0;
        double triangles = 0.0;
        double textureSwitches = 0.0;
        double allocCount = 0.0;
        double allocBytes = 0.0;
        StageSum stages;
        for (const StressSample& sample : _samples) {
            stages.Add(sample);
            summary.cpuMs += sample.cpuMs;
            summary.workMs += sample.workMs;
            drawCalls += sample.drawCalls;
            primitives += static_cast<double>(sample.primitives);
            triangles += static_cast<double>(sample.triangles);
            textureSwitches += sample.textureSwitches;
            allocCount += static_cast<double>(sample.allocCount);
            allocBytes += static_cast<double>(sample.allocBytes);
        }
        const double n = static_cast<double>(_samples.size());
        summary.cpuMs /= n;
        summary.workMs /= n;
        summary.drawCalls = drawCalls / n;
        summary.primitives = primitives / n;
        summary.triangles = triangles / n;
        summary.textureSwitches = textureSwitches / n;
        summary.allocsPerFrame = allocCount / n;
        summary.allocBytesPerFrame = allocBytes / n;
        summary.stages = stages.Mean();
        return summary;
    }

    String FormatNumber(double value) {
        return std::isfinite(value) ? Format("{:.{}f}", value, kDecimals) : String("0");
    }

    String FormatJsonLine(const StressSummary& summary, const StressRunContext& context) {
        String line;
        line.reserve(kLineReserve);
        // Sorted by key (byte order, so `allocBytesPerFrame` < `allocTracking` < `allocsPerFrame`): a reader can diff two lines
        AddNumber(line, "allocBytesPerFrame", summary.allocBytesPerFrame);
        AddBool(line, "allocTracking", context.allocTracking);
        AddNumber(line, "allocsPerFrame", summary.allocsPerFrame);
        AddInteger(line, "atlases", context.atlases);
        AddInteger(line, "batching", context.batching ? 1 : 0);
        AddInteger(line, "count", context.count);
        AddNumber(line, "cpuMs", summary.cpuMs);
        AddNumber(line, "cpuMsP50", summary.cpuMsP50);
        AddNumber(line, "cpuMsP99", summary.cpuMsP99);
        AddNumber(line, "drawCalls", summary.drawCalls);
        AddNumber(line, "drawMs", summary.stages.drawMs);
        AddNumber(line, "drawMsP50", summary.stagesP50.drawMs);
        AddNumber(line, "frameMsP50", summary.frameMsP50);
        AddNumber(line, "frameMsP99", summary.frameMsP99);
        AddInteger(line, "frames", summary.frames);
        AddNumber(line, "gpuMs", summary.stages.gpuMs);
        AddNumber(line, "gpuMsP50", summary.stagesP50.gpuMs);
        AddNumber(line, "gpuMsP99", summary.gpuMsP99);
        AddBool(line, "interleave", context.interleave);
        AddBool(line, "manualClock", context.manualClock);
        AddNumber(line, "presentMs", summary.stages.presentMs);
        AddNumber(line, "presentMsP50", summary.stagesP50.presentMs);
        AddNumber(line, "primitives", summary.primitives);
        AddText(line, "scene", ToFlagValue(context.scene));
        AddInteger(line, "spriteSize", context.load.spriteSize);
        AddNumber(line, "submitMs", summary.stages.submitMs);
        AddNumber(line, "submitMsP50", summary.stagesP50.submitMs);
        AddNumber(line, "textureSwitches", summary.textureSwitches);
        AddNumber(line, "triangles", summary.triangles);
        AddNumber(line, "updateMs", summary.stages.updateMs);
        AddNumber(line, "updateMsP50", summary.stagesP50.updateMs);
        AddNumber(line, "waitMs", summary.stages.waitMs);
        AddNumber(line, "waitMsP50", summary.stagesP50.waitMs);
        AddInteger(line, "warmup", context.warmup);
        AddNumber(line, "workMs", summary.workMs);
        AddNumber(line, "workMsP50", summary.workMsP50);
        AddNumber(line, "workMsP99", summary.workMsP99);
        line.insert(size_t{0}, 1, '{');
        line += '}';
        return line;
    }

}  // namespace BECore::Stress
