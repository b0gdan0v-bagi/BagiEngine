#include "PoolStringsControl.h"

#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolString/PoolStringsAnswers.h>

namespace BECore {

    namespace {

        constexpr StringView kNotAvailable = "not_available";
        constexpr int64_t kDefaultFrames = 60;
        constexpr int64_t kDefaultLimit = 20;
        constexpr int64_t kMaxLimit = 1000;

        struct Request {
            String query;
            Optional<PoolStringOrigin> source;
            Optional<bool> record;
            int64_t frames = kDefaultFrames;
            int64_t limit = kDefaultLimit;
            bool reset = false;
        };

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        /// An integer member from @p min to @p max, or @p fallback when absent
        std::expected<int64_t, ControlError> ReadCount(JsonView params, StringView key, int64_t fallback, int64_t min, int64_t max) {
            if (!FindParam(params, key)) {
                return fallback;
            }
            const auto value = GetParamInt(params, key);
            if (!value || *value < min || *value > max) {
                return InvalidParams(Format("'{}' has to be an integer from {} to {}", key, min, max));
            }
            return *value;
        }

        /// A boolean member, nothing when absent
        std::expected<Optional<bool>, ControlError> ReadFlag(JsonView params, StringView key) {
            if (!FindParam(params, key)) {
                return Optional<bool>();
            }
            const Optional<bool> value = GetParamBool(params, key);
            if (!value) {
                return InvalidParams(Format("'{}' has to be a boolean", key));
            }
            return value;
        }

        std::expected<Request, ControlError> ReadRequest(JsonView params) {
            Request request;
            const auto frames = ReadCount(params, "frames", kDefaultFrames, 1, static_cast<int64_t>(PoolStringMonitor::kFrameHistory));
            if (!frames) {
                return std::unexpected(frames.error());
            }
            request.frames = *frames;
            const auto limit = ReadCount(params, "limit", kDefaultLimit, 0, kMaxLimit);
            if (!limit) {
                return std::unexpected(limit.error());
            }
            request.limit = *limit;
            if (FindParam(params, "query")) {
                Optional<String> query = GetParamString(params, "query");
                if (!query) {
                    return InvalidParams("'query' has to be a string");
                }
                request.query = std::move(*query);
            }
            if (FindParam(params, "source")) {
                const Optional<String> source = GetParamString(params, "source");
                for (const PoolStringOrigin origin : EnumUtils<PoolStringOrigin>::Values()) {
                    if (source && StringView(*source) == GetPoolStringOriginName(origin)) {
                        request.source = origin;
                    }
                }
                if (!request.source) {
                    return InvalidParams("'source' has to be \"static\", \"runtime\" or \"adopted\"");
                }
            }
            const auto record = ReadFlag(params, "record");
            if (!record) {
                return std::unexpected(record.error());
            }
            request.record = *record;
            const auto reset = ReadFlag(params, "reset");
            if (!reset) {
                return std::unexpected(reset.error());
            }
            request.reset = reset->value_or(false);
            return request;
        }

        void HandlePoolStrings(ControlCall& call, PoolStringMonitor& monitor) {
            const auto request = ReadRequest(call.Params());
            if (!request) {
                call.Fail(request.error());
                return;
            }
            if (request->record) {
                if (!kEditorBuilt) {
                    call.Fail(kNotAvailable, "'record' is a switch of the editor: this build has none (BE_WITH_EDITOR=OFF)");
                    return;
                }
                monitor.SetRecording(*request->record);
            }

            // The answer points into the snapshot
            PoolStringSnapshot snapshot;
            monitor.GetStorage().TakeSnapshot(snapshot);
            PoolStringQuery query;
            query.text = request->query;
            query.origin = request->source;

            PoolStringsAnswer answer;
            answer.checks = PoolStringsChecks::Read(snapshot);
            answer.growth = PoolStringsGrowth::Read(monitor, static_cast<size_t>(request->frames));
            answer.recording = PoolStringsRecording::Read(monitor);
            answer.search = PoolStringsSearch::Read(snapshot, query, static_cast<size_t>(request->limit));
            answer.summary = PoolStringsSummary::Read(snapshot);
            call.ReplyObject(answer);
            if (request->reset) {
                monitor.ClearRecords();
            }
        }

    }  // namespace

    void RegisterPoolStringsControl(ControlService& service, PoolStringMonitor& monitor) {
        service.RegisterHandler("pool_strings", [&monitor](ControlCall& call) { HandlePoolStrings(call, monitor); });

        service.DescribeMethod("pool_strings", {"The PoolString pool: summary (statistics and blocks), growth over the last frames, the recorded runtime strings by profiler zone, checks "
                                                "(hash collisions, texts held twice) and a search over every string",
                                                {{"frames", "integer", false, Format("How many frames of growth, 1 to {} (default {})", PoolStringMonitor::kFrameHistory, kDefaultFrames)},
                                                 {"limit", "integer", false, Format("How many search rows, 0 to {} (default {})", kMaxLimit, kDefaultLimit)},
                                                 {"query", "string", false, "A part of the text, ASCII case ignored (default: every string)"},
                                                 {"source", "string", false, "Search only \"static\", \"runtime\" or \"adopted\" strings"},
                                                 {"record", "boolean", false, "Switch the recording of new runtime strings before answering (a build with the editor)"},
                                                 {"reset", "boolean", false, "Clear the records after answering"}},
                                                /*quiet=*/true});
    }

}  // namespace BECore
