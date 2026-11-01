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

        void HandleStatus(ControlCall& call) {
            const Time& time = CoreManager::GetTime();
            Json result = Json::object();
            result["frame"] = time.GetFrameCount();
            result["totalTime"] = time.GetTotalTime();
            result["deltaTime"] = time.GetDeltaTime();
            result["fixedDeltaTime"] = time.GetFixedDeltaTime();
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
        service.RegisterHandler("status", HandleStatus);
        service.RegisterHandler("quit", HandleQuit, /*bypassBarrier=*/true);
    }

}  // namespace BECore
