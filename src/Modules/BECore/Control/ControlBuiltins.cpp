#include "ControlBuiltins.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Events/ApplicationEvents.h>

namespace BECore {

    namespace {

        void HandlePing(ControlCall& call) {
            Json result = Json::object();
            result["pong"] = true;
            result["protocol"] = ControlService::kProtocolVersion;
            call.Reply(std::move(result));
        }

        /// The frame clock as the answers of `status` and `step` report it
        Json DescribeClock() {
            const Time& time = CoreManager::GetTime();
            Json clock = Json::object();
            clock["frame"] = time.GetFrameCount();
            clock["totalTime"] = time.GetTotalTime();
            clock["deltaTime"] = time.GetDeltaTime();
            clock["fixedDeltaTime"] = time.GetFixedDeltaTime();
            return clock;
        }

        void HandleStatus(ControlCall& call, const ControlService& service) {
            Json result = DescribeClock();
            result["manualClock"] = service.IsManualClock();
            const GamePause* pause = service.GetPause();
            result["paused"] = pause != nullptr && pause->IsPaused();
            call.Reply(std::move(result));
        }

        /// The frames that a `step` asks for, or the error to answer with. A member that is absent keeps its default
        std::expected<uint32_t, ControlError> ReadStepFrames(const Json& params) {
            if (FindJsonMember(params, "frames") == nullptr) {
                return 1u;
            }
            const auto frames = GetJsonInt(params, "frames");
            if (!frames || *frames < 1 || *frames > kMaxStepFrames) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'frames' has to be an integer from 1 to {}", kMaxStepFrames)));
            }
            return static_cast<uint32_t>(*frames);
        }

        std::expected<double, ControlError> ReadStepSeconds(const Json& params) {
            if (FindJsonMember(params, "dt") == nullptr) {
                return kDefaultStepSeconds;
            }
            const auto seconds = GetJsonNumber(params, "dt");
            if (!seconds || !(*seconds >= 0.0 && *seconds <= Time::kMaxFrameTime)) {  // written so that NaN fails too
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'dt' has to be a number of seconds from 0 to {}", Time::kMaxFrameTime)));
            }
            return *seconds;
        }

        void HandleStep(ControlCall& call, const ControlService& service) {
            const auto frames = ReadStepFrames(call.Params());
            if (!frames) {
                call.Fail(frames.error());
                return;
            }
            const auto seconds = ReadStepSeconds(call.Params());
            if (!seconds) {
                call.Fail(seconds.error());
                return;
            }
            // A paused real clock steps `frames` frames of dt itself; the frame that runs this handler ends too and counts, hence one more.
            // The manual clock requests its frames when the call is deferred
            GamePause* pause = service.GetPause();
            const bool stepsPausedGame = pause != nullptr && pause->IsPaused() && !service.IsManualClock();
            if (stepsPausedGame) {
                pause->RequestSteps(*frames, *seconds);
            }
            const uint32_t countedFrames = stepsPausedGame ? *frames + 1 : *frames;
            call.DeferFrames(countedFrames, *seconds, [](ControlCall& done) { done.Reply(DescribeClock()); });
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
        service.RegisterHandler("quit", HandleQuit, /*bypassBarrier=*/true);
        RegisterReflectionControl(service);

        service.DescribeMethod("ping", {"Answers pong and the protocol version", {}});
        service.DescribeMethod("status", {"The frame clock, whether the clock is manual and whether the game is paused", {}});
        service.DescribeMethod("step", {"Runs frames and answers with the clock after the last one",
                                        {{"frames", "integer", false, Format("Frames to run, 1 to {} (default 1)", kMaxStepFrames)},
                                         {"dt", "number", false, Format("Seconds each frame lasts, 0 to {} (default 1/60)", Time::kMaxFrameTime)}}});
        service.DescribeMethod("pause", {"Holds the game from the next frame", {}});
        service.DescribeMethod("resume", {"Lets the game run again (refused with the manual clock)", {}});
        service.DescribeMethod("list_methods", {"Lists the methods with their summaries and parameters", {}});
        service.DescribeMethod("quit", {"Answers, then stops the application at the end of the frame", {}});
    }

}  // namespace BECore
