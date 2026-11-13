#include "StressRecorder.h"

#include <EASTL/sort.h>

namespace BECore::Stress {

    namespace {

        constexpr double kRankNudge = 1e-9;
        constexpr double kMedian = 0.5;
        constexpr double kTail = 0.99;
        constexpr int kDecimals = 4;
        /// Room for the fixed line (under 400 characters), so that writing it does not grow the string
        constexpr size_t kLineReserve = 512;

        /// The sorted values of one field of the samples
        template <typename Field>
        eastl::vector<double> SortedOf(const eastl::vector<StressSample>& samples, Field field) {
            eastl::vector<double> values;
            values.reserve(samples.size());
            for (const StressSample& sample : samples) {
                values.push_back(sample.*field);
            }
            eastl::sort(values.begin(), values.end());
            return values;
        }

        /// A `"key":value` member (a number written by FormatNumber); the keys are added in sorted order by the caller
        void AddMember(eastl::string& line, eastl::string_view key, const eastl::string& value) {
            if (!line.empty()) {
                line += ',';
            }
            line += '"';
            line.append(key.data(), key.size());
            line += "\":";
            line += value;
        }

        void AddNumber(eastl::string& line, eastl::string_view key, double value) {
            AddMember(line, key, FormatNumber(value));
        }

        void AddInteger(eastl::string& line, eastl::string_view key, uint32_t value) {
            AddMember(line, key, Format("{}", value));
        }

        void AddBool(eastl::string& line, eastl::string_view key, bool value) {
            AddMember(line, key, value ? "true" : "false");
        }

        void AddText(eastl::string& line, eastl::string_view key, eastl::string_view value) {
            eastl::string quoted("\"");
            quoted.append(value.data(), value.size());
            quoted += '"';
            AddMember(line, key, quoted);
        }

    }  // namespace

    StressSample SampleOf(const FrameStats& stats) {
        StressSample sample;
        sample.cpuMs = stats.cpuMs;
        sample.workMs = std::max(0.0, stats.cpuMs - stats.GetStageMs(FrameStage::Present));
        sample.frameMs = stats.frameMs;
        sample.drawCalls = stats.draws.drawCalls;
        sample.triangles = stats.draws.triangles;
        sample.textureSwitches = stats.draws.textureSwitches;
        sample.allocCount = stats.allocCount;
        sample.allocBytes = stats.allocBytes;
        return sample;
    }

    double NearestRank(eastl::span<const double> sorted, double fraction) {
        if (sorted.empty()) {
            return 0.0;
        }
        const double rank = std::ceil(fraction * static_cast<double>(sorted.size()) - kRankNudge);
        const size_t index = rank < 1.0 ? 0 : std::min(sorted.size(), static_cast<size_t>(rank)) - 1;
        return sorted[index];
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

        double drawCalls = 0.0;
        double triangles = 0.0;
        double textureSwitches = 0.0;
        double allocCount = 0.0;
        double allocBytes = 0.0;
        for (const StressSample& sample : _samples) {
            summary.cpuMs += sample.cpuMs;
            summary.workMs += sample.workMs;
            drawCalls += sample.drawCalls;
            triangles += static_cast<double>(sample.triangles);
            textureSwitches += sample.textureSwitches;
            allocCount += static_cast<double>(sample.allocCount);
            allocBytes += static_cast<double>(sample.allocBytes);
        }
        const double n = static_cast<double>(_samples.size());
        summary.cpuMs /= n;
        summary.workMs /= n;
        summary.drawCalls = drawCalls / n;
        summary.triangles = triangles / n;
        summary.textureSwitches = textureSwitches / n;
        summary.allocsPerFrame = allocCount / n;
        summary.allocBytesPerFrame = allocBytes / n;
        return summary;
    }

    eastl::string FormatNumber(double value) {
        return std::isfinite(value) ? Format("{:.{}f}", value, kDecimals) : eastl::string("0");
    }

    eastl::string FormatJsonLine(const StressSummary& summary, const StressRunContext& context) {
        eastl::string line;
        line.reserve(kLineReserve);
        // Sorted by key (byte order, so `allocBytesPerFrame` < `allocTracking` < `allocsPerFrame`): a reader can diff two lines
        AddNumber(line, "allocBytesPerFrame", summary.allocBytesPerFrame);
        AddBool(line, "allocTracking", context.allocTracking);
        AddNumber(line, "allocsPerFrame", summary.allocsPerFrame);
        AddInteger(line, "atlases", context.atlases);
        AddInteger(line, "count", context.count);
        AddNumber(line, "cpuMs", summary.cpuMs);
        AddNumber(line, "cpuMsP50", summary.cpuMsP50);
        AddNumber(line, "cpuMsP99", summary.cpuMsP99);
        AddNumber(line, "drawCalls", summary.drawCalls);
        AddNumber(line, "frameMsP50", summary.frameMsP50);
        AddNumber(line, "frameMsP99", summary.frameMsP99);
        AddInteger(line, "frames", summary.frames);
        AddBool(line, "interleave", context.interleave);
        AddBool(line, "manualClock", context.manualClock);
        AddText(line, "scene", ToFlagValue(context.scene));
        AddNumber(line, "textureSwitches", summary.textureSwitches);
        AddNumber(line, "triangles", summary.triangles);
        AddInteger(line, "warmup", context.warmup);
        AddNumber(line, "workMs", summary.workMs);
        AddNumber(line, "workMsP50", summary.workMsP50);
        AddNumber(line, "workMsP99", summary.workMsP99);
        line.insert(size_t{0}, 1, '{');
        line += '}';
        return line;
    }

}  // namespace BECore::Stress
