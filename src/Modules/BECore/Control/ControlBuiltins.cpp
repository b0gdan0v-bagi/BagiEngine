#include "ControlBuiltins.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlWindow.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Time/Time.h>
#include <Events/ApplicationEvents.h>

namespace BECore {

    Json DescribeControlClock() {
        const Time& time = CoreManager::GetTime();
        Json clock = Json::object();
        clock["frame"] = time.GetFrameCount();
        clock["totalTime"] = time.GetTotalTime();
        clock["deltaTime"] = time.GetDeltaTime();
        clock["fixedDeltaTime"] = time.GetFixedDeltaTime();
        return clock;
    }

    std::expected<uint32_t, ControlError> ReadControlFrames(const Json& params, StringView key, uint32_t fallback) {
        if (FindJsonMember(params, key) == nullptr) {
            return fallback;
        }
        const auto frames = GetJsonInt(params, key);
        if (!frames || *frames < 1 || *frames > kMaxStepFrames) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be an integer from 1 to {}", key, kMaxStepFrames)));
        }
        return static_cast<uint32_t>(*frames);
    }

    std::expected<double, ControlError> ReadControlFrameSeconds(const Json& params) {
        if (FindJsonMember(params, "dt") == nullptr) {
            return kDefaultStepSeconds;
        }
        const auto seconds = GetJsonNumber(params, "dt");
        if (!seconds || !(*seconds >= 0.0 && *seconds <= Time::kMaxFrameTime)) {  // written so that NaN fails too
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'dt' has to be a number of seconds from 0 to {}", Time::kMaxFrameTime)));
        }
        return *seconds;
    }

    namespace {

        void HandlePing(ControlCall& call) {
            Json result = Json::object();
            result["pong"] = true;
            result["protocol"] = ControlService::kProtocolVersion;
            call.Reply(std::move(result));
        }

        Json DescribeLogTotals(const ControlLog& log) {
            Json totals = Json::object();
            totals["warnings"] = log.GetWarnings();
            totals["errors"] = log.GetErrors();
            return totals;
        }

        void HandleStatus(ControlCall& call, const ControlService& service) {
            // A line logged earlier in this frame is still in the ring buffer: handlers run outside Flush(), on the thread that flushes
            LogEvent::Flush();
            Json result = DescribeControlClock();
            result["log"] = DescribeLogTotals(service.Log());
            result["manualClock"] = service.IsManualClock();
            const GamePause* pause = service.GetPause();
            result["paused"] = pause != nullptr && pause->IsPaused();
            result["window"] = DescribeWindowStatus();
            call.Reply(std::move(result));
        }

        void HandleStep(ControlCall& call, ControlService& service) {
            const auto frames = ReadControlFrames(call.Params(), "frames", 1);
            if (!frames) {
                call.Fail(frames.error());
                return;
            }
            const auto seconds = ReadControlFrameSeconds(call.Params());
            if (!seconds) {
                call.Fail(seconds.error());
                return;
            }
            service.DeferSteppedFrames(call, *frames, *seconds, [](ControlCall& done) { done.Reply(DescribeControlClock()); });
        }

        /// `pause` and `resume`: the pause is the one the editor drives too. The manual clock is a pause that only `step` lifts frames out of, so it is always paused
        void HandlePause(ControlCall& call, const ControlService& service) {
            GamePause* pause = service.GetPause();
            if (pause == nullptr) {
                call.Fail(ControlErrors::kInternalError, "this service has no pause to drive");
                return;
            }
            pause->SetPaused(true);
            Json result = Json::object();
            result["paused"] = true;
            call.Reply(std::move(result));
        }

        void HandleResume(ControlCall& call, const ControlService& service) {
            GamePause* pause = service.GetPause();
            if (pause == nullptr) {
                call.Fail(ControlErrors::kInternalError, "this service has no pause to drive");
                return;
            }
            if (service.IsManualClock()) {
                call.Fail(ControlErrors::kManualClock, "the manual clock is a pause that only `step` advances; it cannot be resumed");
                return;
            }
            pause->SetPaused(false);
            Json result = Json::object();
            result["paused"] = false;
            call.Reply(std::move(result));
        }

        /// `list_methods`: every method of the service with the description its module gave it. The answer is built from the table only, no client data is read
        void HandleListMethods(ControlCall& call, const ControlService& service) {
            Json methods = Json::array();
            for (const ControlMethodListing& listing : service.GetMethods()) {
                Json method = Json::object();
                method["name"] = MakeJsonString(listing.name);
                method["summary"] = MakeJsonString(listing.info != nullptr ? StringView(listing.info->summary) : StringView());
                Json params = Json::array();
                if (listing.info != nullptr) {
                    for (const ControlParamInfo& param : listing.info->params) {
                        Json entry = Json::object();
                        entry["name"] = MakeJsonString(param.name);
                        entry["type"] = MakeJsonString(param.type);
                        entry["required"] = param.required;
                        entry["description"] = MakeJsonString(param.description);
                        params.push_back(std::move(entry));
                    }
                }
                method["params"] = std::move(params);
                methods.push_back(std::move(method));
            }
            Json result = Json::object();
            result["methods"] = std::move(methods);
            call.Reply(std::move(result));
        }

        constexpr StringView kBatchMethod = "batch";
        constexpr StringView kQuitMethod = "quit";
        constexpr StringView kWarningLevelName = "warning";
        constexpr StringView kErrorLevelName = "error";

        /// The lowercase name a line's level has in the answers: Warning, Error and Fatal are the levels the log keeps
        StringView LogLevelName(LogLevel level) {
            switch (level) {
                case LogLevel::Warning:
                    return kWarningLevelName;
                case LogLevel::Error:
                    return kErrorLevelName;
                default:
                    return "fatal";
            }
        }

        /// The `level` filter of log_since and log_subscribe: "warning" (the default) or "error"
        std::expected<LogLevel, ControlError> ReadLogLevel(const Json& params) {
            if (FindJsonMember(params, "level") == nullptr) {
                return LogLevel::Warning;
            }
            const auto name = GetJsonString(params, "level");
            if (name && *name == kWarningLevelName) {
                return LogLevel::Warning;
            }
            if (name && *name == kErrorLevelName) {
                return LogLevel::Error;
            }
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'level' has to be \"warning\" or \"error\""));
        }

        void HandleLogMark(ControlCall& call, const ControlService& service) {
            LogEvent::Flush();
            const ControlLog& log = service.Log();
            Json result = Json::object();
            result["mark"] = log.GetNextSequence();
            result["warnings"] = log.GetWarnings();
            result["errors"] = log.GetErrors();
            call.Reply(std::move(result));
        }

        Json DescribeLogLine(const LogHistory& history, const LogRecord& record) {
            Json line = Json::object();
            line["sequence"] = record.sequence;
            line["level"] = MakeJsonString(LogLevelName(record.level));
            line["time"] = MakeJsonString(record.time);
            line["message"] = MakeJsonString(StringView(record.message.data(), record.message.size()));
            if (record.category != kGeneralLogCategory) {
                line["category"] = MakeJsonString(history.GetCategoryName(record.category));
            }
            return line;
        }

        void HandleLogSince(ControlCall& call, const ControlService& service) {
            const auto mark = GetJsonInt(call.Params(), "mark");
            if (!mark || *mark < 0) {
                call.Fail(ControlErrors::kInvalidParams, "'mark' has to be an integer, 0 or more (the mark of log_mark)");
                return;
            }
            const auto level = ReadLogLevel(call.Params());
            if (!level) {
                call.Fail(level.error());
                return;
            }

            LogEvent::Flush();
            const ControlLog& log = service.Log();
            const ControlLog::Since since = log.ReadSince(static_cast<uint64_t>(*mark), *level, ControlLog::kMaxLogSinceLines);

            Json lines = Json::array();
            for (const LogRecord* record : since.lines) {
                lines.push_back(DescribeLogLine(log.History(), *record));
            }
            Json result = Json::object();
            result["lines"] = std::move(lines);
            result["warnings"] = since.warnings;
            result["errors"] = since.errors;
            result["dropped"] = since.dropped;
            result["next"] = since.next;
            if (since.truncated) {
                result["truncated"] = true;
            }
            call.Reply(std::move(result));
        }

        void HandleLogSubscribe(ControlCall& call, ControlService& service) {
            const auto enabled = GetJsonBool(call.Params(), "enabled");
            if (!enabled) {
                call.Fail(ControlErrors::kInvalidParams, "'enabled' has to be a boolean");
                return;
            }
            const auto level = ReadLogLevel(call.Params());
            if (!level) {
                call.Fail(level.error());
                return;
            }

            ControlLog& log = service.Log();
            ControlLog::PushState& push = log.Push();
            if (*enabled && !push.enabled) {
                push.cursor = log.GetNextSequence();  // no replay: it starts with the next line
            }
            push.enabled = *enabled;
            push.minLevel = *level;

            Json result = Json::object();
            result["enabled"] = push.enabled;
            result["level"] = MakeJsonString(LogLevelName(push.minLevel));
            call.Reply(std::move(result));
        }

        /// Frame end hook `log_push`: one `log` event per new line while log_subscribe is on. It allocates nothing when no line is new
        void PushLogLines(ControlService& service) {
            ControlLog& log = service.Log();
            ControlLog::PushState& push = log.Push();
            if (!push.enabled) {
                return;
            }
            const LogHistory& history = log.History();
            const uint64_t next = history.GetNextSequence();
            if (push.cursor >= next) {
                return;
            }

            const uint64_t oldest = next - history.Size();
            const uint64_t first = std::max(push.cursor, oldest);
            push.cursor = next;

            uint64_t matching = 0;
            for (uint64_t sequence = first; sequence < next; ++sequence) {
                matching += history.At(static_cast<size_t>(sequence - oldest)).level >= push.minLevel ? 1 : 0;
            }

            uint32_t pushed = 0;
            for (uint64_t sequence = first; sequence < next && pushed < ControlLog::kMaxLogPushPerFrame; ++sequence) {
                const LogRecord& record = history.At(static_cast<size_t>(sequence - oldest));
                if (record.level < push.minLevel) {
                    continue;
                }
                Json data = DescribeLogLine(history, record);
                ++pushed;
                if (pushed == ControlLog::kMaxLogPushPerFrame && matching > pushed) {
                    data["skipped"] = matching - pushed;
                }
                service.Publish("log", data);
            }
        }

        /// One call of a `batch`, checked before anything runs
        struct BatchEntry {
            String method;
            Json params;
        };

        /// The state of a `batch` that is running. It lives in the continuation of the call that waits for frames, not in the handler, which has returned by then
        struct BatchState {
            Vector<BatchEntry> entries;
            bool continueOnError = false;
            size_t next = 0;
            Json results = Json::array();
            Optional<size_t> stopped;

            [[nodiscard]] bool IsFinished() const {
                return next >= entries.size() || stopped.has_value();
            }

            void Record(ControlAnswer answer) {
                Json entry = Json::object();
                entry["ok"] = answer.has_value();
                if (answer) {
                    entry["result"] = std::move(*answer);
                } else {
                    Json error = Json::object();
                    error["code"] = MakeJsonString(answer.error().code);
                    error["message"] = MakeJsonString(answer.error().message);
                    entry["error"] = std::move(error);
                }
                results.push_back(std::move(entry));
                if (!answer && !continueOnError) {
                    stopped = next;
                }
                ++next;
            }
        };

        /// The calls of a batch as the client sent them, or the error to answer with. Nothing is built from a bad shape
        std::expected<BatchState, ControlError> ReadBatch(const Json& params) {
            const Json* calls = FindJsonMember(params, "calls");
            if (calls == nullptr || !calls->is_array() || calls->empty() || calls->size() > kMaxBatchCalls) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'calls' has to be an array of 1 to {} calls", kMaxBatchCalls)));
            }

            BatchState state;
            if (FindJsonMember(params, "continueOnError") != nullptr) {
                const auto continueOnError = GetJsonBool(params, "continueOnError");
                if (!continueOnError) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'continueOnError' has to be a boolean"));
                }
                state.continueOnError = *continueOnError;
            }

            state.entries.reserve(calls->size());
            for (const Json& call : *calls) {
                auto method = GetJsonString(call, "method");
                if (!call.is_object() || !method || method->empty()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "every call of 'calls' has to be an object with a non-empty string 'method'"));
                }
                const Json* callParams = FindJsonMember(call, "params");
                if (callParams != nullptr && !callParams->is_null() && !callParams->is_object()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'params' of a call has to be an object"));
                }
                state.entries.push_back(BatchEntry{std::move(*method), callParams != nullptr && callParams->is_object() ? *callParams : Json::object()});
            }
            return state;
        }

        /// Runs the calls from `next` on until one defers (the continuation then carries on after its frames) or the batch is finished and answered
        void RunBatch(ControlService& service, ControlCall& outer, UniquePtr<BatchState> state) {
            BatchState* batch = state.get();
            ControlService::NestedContinuation resume = [&service, state = std::move(state)](ControlCall& again, ControlAnswer answer) mutable {
                state->Record(std::move(answer));
                RunBatch(service, again, std::move(state));
            };

            while (!batch->IsFinished()) {
                const BatchEntry& entry = batch->entries[batch->next];
                if (entry.method == kBatchMethod || entry.method == kQuitMethod) {
                    batch->Record(std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' cannot run inside a batch", entry.method))));
                    continue;
                }
                auto answer = service.RunNested(outer, entry.method, entry.params, std::move(resume));
                if (!answer) {
                    return;  // the call waits for frames: `resume` has the state now
                }
                batch->Record(std::move(*answer));
            }

            Json result = Json::object();
            result["results"] = std::move(batch->results);
            if (batch->stopped) {
                result["stopped"] = *batch->stopped;
            }
            outer.Reply(std::move(result));
        }

        void HandleBatch(ControlCall& call, ControlService& service) {
            auto state = ReadBatch(call.Params());
            if (!state) {
                call.Fail(state.error());
                return;
            }
            RunBatch(service, call, MakeUnique<BatchState>(std::move(*state)));
        }

        void HandleQuit(ControlCall& call) {
            // The answer first: it is queued for the transport before the application starts to stop
            call.Reply(Json::object());
            ApplicationEvents::QuitEvent::Emit();
        }

    }  // namespace

    void RegisterControlBuiltins(ControlService& service) {
        service.RegisterHandler("ping", HandlePing);
        service.RegisterHandler("status", [&service](ControlCall& call) { HandleStatus(call, service); });
        service.RegisterHandler("step", [&service](ControlCall& call) { HandleStep(call, service); });
        service.RegisterHandler("pause", [&service](ControlCall& call) { HandlePause(call, service); });
        service.RegisterHandler("resume", [&service](ControlCall& call) { HandleResume(call, service); });
        service.RegisterHandler("list_methods", [&service](ControlCall& call) { HandleListMethods(call, service); });
        service.RegisterHandler("log_mark", [&service](ControlCall& call) { HandleLogMark(call, service); });
        service.RegisterHandler("log_since", [&service](ControlCall& call) { HandleLogSince(call, service); });
        service.RegisterHandler("log_subscribe", [&service](ControlCall& call) { HandleLogSubscribe(call, service); });
        service.RegisterHandler("batch", [&service](ControlCall& call) { HandleBatch(call, service); });
        service.RegisterHandler("quit", HandleQuit, /*bypassBarrier=*/true);
        service.SetFrameEndHook("log_push", [&service] { PushLogLines(service); });
        service.Log().Start();
        RegisterReflectionControl(service);
        RegisterWindowControl(service);

        service.DescribeMethod("ping", {"Answers pong and the protocol version", {}});
        service.DescribeMethod("status", {"The frame clock, whether the clock is manual, whether the game is paused, the totals of warnings and errors logged since the start and the window (size, "
                                          "density, safe area, output, user scale)",
                                          {}});
        service.DescribeMethod("step", {"Runs frames and answers with the clock after the last one",
                                        {{"frames", "integer", false, Format("Frames to run, 1 to {} (default 1)", kMaxStepFrames)},
                                         {"dt", "number", false, Format("Seconds each frame lasts, 0 to {} (default 1/60)", Time::kMaxFrameTime)}}});
        service.DescribeMethod("pause", {"Holds the game from the next frame", {}});
        service.DescribeMethod("resume", {"Lets the game run again (refused with the manual clock)", {}});
        service.DescribeMethod("list_methods", {"Lists the methods with their summaries and parameters", {}});
        service.DescribeMethod("log_mark", {"A mark for log_since and the totals of warnings and errors so far", {}});
        service.DescribeMethod(
            "log_since", {Format("The warnings and errors logged since a mark, the newest {} at most, with the counts of all of them", ControlLog::kMaxLogSinceLines),
                          {{"mark", "integer", true, "The mark of log_mark (0 is the start)"}, {"level", "string", false, "\"warning\" (default: warnings and errors) or \"error\" (errors only)"}}});
        service.DescribeMethod("log_subscribe",
                               {Format("Turns on or off the push of the event `log`, one per new line, at most {} per frame (off by default, for every client)", ControlLog::kMaxLogPushPerFrame),
                                {{"enabled", "boolean", true, "Push on or off"}, {"level", "string", false, "\"warning\" (default) or \"error\""}}});
        service.DescribeMethod(
            "batch",
            {Format(
                 "Runs up to {} calls in order in one request and answers with the answer of each; an error ends it unless continueOnError. A call that waits for frames (step) makes the batch wait",
                 kMaxBatchCalls),
             {{"calls", "array", true, "Objects {method, params?}; `batch` and `quit` are refused inside"},
              {"continueOnError", "boolean", false, "Keep running after a call that fails (default false)"}}});
        service.DescribeMethod("quit", {"Answers, then stops the application at the end of the frame", {}});
    }

}  // namespace BECore
