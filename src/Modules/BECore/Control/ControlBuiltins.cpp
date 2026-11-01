#include "ControlBuiltins.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
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

        void HandleStep(ControlCall& call) {
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
            call.DeferFrames(*frames, *seconds, [](ControlCall& done) { done.Reply(DescribeClock()); });
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
        service.RegisterHandler("step", HandleStep);
        service.RegisterHandler("quit", HandleQuit, /*bypassBarrier=*/true);
    }

}  // namespace BECore
