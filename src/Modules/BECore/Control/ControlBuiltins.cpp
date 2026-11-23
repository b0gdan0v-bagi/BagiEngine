#include "ControlBuiltins.h"

#include <BECore/Control/ControlBuiltinAnswers.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlWindow.h>
#include <BECore/Control/ControlWindowStatus.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Time/Time.h>
#include <BECore/Values/Explain/ValueExplainControl.h>
#include <Events/ApplicationEvents.h>

namespace BECore {

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
            ControlPingAnswer answer;
            answer.protocol = ControlService::kProtocolVersion;
            call.ReplyObject(answer);
        }

        void HandleStatus(ControlCall& call, const ControlService& service) {
            // A line logged earlier in this frame is still in the ring buffer: handlers run outside Flush(), on the thread that flushes
            LogEvent::Flush();
            const GamePause* pause = service.GetPause();
            ControlStatusAnswer answer;
            static_cast<ControlClock&>(answer) = ControlClock::Read();
            answer.log.errors = service.Log().GetErrors();
            answer.log.warnings = service.Log().GetWarnings();
            answer.manualClock = service.IsManualClock();
            answer.paused = pause != nullptr && pause->IsPaused();
            answer.window = ControlWindowStatus::Read();
            call.ReplyObject(answer);
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
            service.DeferSteppedFrames(call, *frames, *seconds, [](ControlCall& done) { done.ReplyObject(ControlClock::Read()); });
        }

        /// `pause` and `resume`: the pause is the one the editor drives too. The manual clock is a pause that only `step` lifts frames out of, so it is always paused
        void HandlePause(ControlCall& call, const ControlService& service) {
            GamePause* pause = service.GetPause();
            if (pause == nullptr) {
                call.Fail(ControlErrors::kInternalError, "this service has no pause to drive");
                return;
            }
            pause->SetPaused(true);
            ControlPausedAnswer answer;
            answer.paused = true;
            call.ReplyObject(answer);
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
            call.ReplyObject(ControlPausedAnswer());
        }

        /// `list_methods`: every method of the service with the description its module gave it. The answer is built from the table only, no client data is read
        void HandleListMethods(ControlCall& call, const ControlService& service) {
            ControlMethodsAnswer answer;
            for (const ControlMethodListing& listing : service.GetMethods()) {
                ControlListedMethod& method = answer.methods.emplace_back();
                method.name = listing.name;
                if (listing.info == nullptr) {
                    continue;
                }
                method.summary = listing.info->summary;
                for (const ControlParamInfo& param : listing.info->params) {
                    ControlListedParam& entry = method.params.emplace_back();
                    entry.description = param.description;
                    entry.name = param.name;
                    entry.required = param.required;
                    entry.type = param.type;
                }
            }
            call.ReplyObject(answer);
        }

        constexpr StringView kBatchMethod = "batch";
        constexpr StringView kQuitMethod = "quit";

        /// The names of the levels the log keeps, as the answers and the `level` filter spell them
        struct LogLevelNames {
            static constexpr auto warning = "warning"_intern;
            static constexpr auto error = "error"_intern;
            static constexpr auto fatal = "fatal"_intern;
        };

        /// The lowercase name a line's level has in the answers: Warning, Error and Fatal are the levels the log keeps
        PoolString LogLevelName(LogLevel level) {
            switch (level) {
                case LogLevel::Warning:
                    return LogLevelNames::warning;
                case LogLevel::Error:
                    return LogLevelNames::error;
                default:
                    return LogLevelNames::fatal;
            }
        }

        /// The `level` filter of log_since and log_subscribe: "warning" (the default) or "error"
        std::expected<LogLevel, ControlError> ReadLogLevel(const Json& params) {
            if (FindJsonMember(params, "level") == nullptr) {
                return LogLevel::Warning;
            }
            const auto name = GetJsonString(params, "level");
            if (name && *name == LogLevelNames::warning.View()) {
                return LogLevel::Warning;
            }
            if (name && *name == LogLevelNames::error.View()) {
                return LogLevel::Error;
            }
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'level' has to be \"warning\" or \"error\""));
        }

        void HandleLogMark(ControlCall& call, const ControlService& service) {
            LogEvent::Flush();
            const ControlLog& log = service.Log();
            ControlLogMarkAnswer answer;
            answer.errors = log.GetErrors();
            answer.mark = log.GetNextSequence();
            answer.warnings = log.GetWarnings();
            call.ReplyObject(answer);
        }

        /// One line of the log, as `log_since` lists it and `log` pushes it. By hand, not a reflected struct: the lines stream from the ring buffer of `LogHistory` and a struct
        /// would copy up to `ControlLog::kMaxLogSinceLines` messages per reply; @p skipped (0: none) is the count of lines a push left out after this one
        void WriteLogLine(JsonWriter& writer, const LogHistory& history, const LogRecord& record, uint64_t skipped) {
            writer.BeginObject();
            if (record.category != kGeneralLogCategory) {
                writer.Member("category", history.GetCategoryName(record.category));
            }
            writer.Member("level", LogLevelName(record.level).ToStringView());
            writer.Member("message", StringView(record.message.data(), record.message.size()));
            writer.Member("sequence", record.sequence);
            if (skipped != 0) {
                writer.Member("skipped", skipped);
            }
            writer.Member("time", StringView(record.time));
            writer.EndObject();
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

            // By hand for the same reason as WriteLogLine: `lines` streams from the ring buffer between the counts, and a struct would copy it
            call.Reply([&](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("dropped", since.dropped);
                writer.Member("errors", since.errors);
                writer.Key("lines");
                writer.BeginArray();
                for (const LogRecord* record : since.lines) {
                    WriteLogLine(writer, log.History(), *record, 0);
                }
                writer.EndArray();
                writer.Member("next", since.next);
                if (since.truncated) {
                    writer.Member("truncated", true);
                }
                writer.Member("warnings", since.warnings);
                writer.EndObject();
            });
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

            ControlLogSubscribeAnswer answer;
            answer.enabled = push.enabled;
            answer.level = LogLevelName(push.minLevel);
            call.ReplyObject(answer);
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
                ++pushed;
                const uint64_t skipped = pushed == ControlLog::kMaxLogPushPerFrame && matching > pushed ? matching - pushed : 0;
                service.Publish("log", [&](JsonWriter& writer) { WriteLogLine(writer, history, record, skipped); });
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
            Vector<ControlAnswer> results;
            Optional<size_t> stopped;

            [[nodiscard]] bool IsFinished() const {
                return next >= entries.size() || stopped.has_value();
            }

            void Record(ControlAnswer answer) {
                const bool failed = !answer;
                results.push_back(std::move(answer));
                if (failed && !continueOnError) {
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

            ControlBatchAnswer answer;
            answer.results.reserve(batch->results.size());
            for (const ControlAnswer& done : batch->results) {
                ControlBatchResult& row = answer.results.emplace_back();
                row.ok = done.has_value();
                if (done) {
                    row.result = JsonPayload::Borrowed(*done);
                } else {
                    ControlErrorAnswer error;
                    error.code = done.error().code;
                    error.message = done.error().message;
                    row.error = std::move(error);
                }
            }
            if (batch->stopped) {
                answer.stopped = *batch->stopped;
            }
            outer.ReplyObject(answer);
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
        RegisterValueExplainControl(service);
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
