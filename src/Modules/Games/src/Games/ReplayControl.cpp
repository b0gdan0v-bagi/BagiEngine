#include "ReplayControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/IControllableGame.h>
#include <Games/SessionRecorder.h>

namespace BECore {

    namespace {

        void HandleRecordStart(ControlCall& call) {
            const auto path = GetJsonString(call.Params(), "path");
            if (!path || path->empty()) {
                call.Fail(ControlErrors::kInvalidParams, "'path' has to be a non-empty string");
                return;
            }
            String title;
            if (FindJsonMember(call.Params(), "title") != nullptr) {
                const auto requested = GetJsonString(call.Params(), "title");
                if (!requested) {
                    call.Fail(ControlErrors::kInvalidParams, "'title' has to be a string");
                    return;
                }
                title = *requested;
            }

            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                call.Fail(GameControlErrors::kNoActiveGame, "no game is active");
                return;
            }
            if (auto started = SessionRecorder::GetInstance().Start(*path, *game, title); !started) {
                call.Fail(started.error());
                return;
            }

            Json result = Json::object();
            result["path"] = MakeJsonString(*path);
            result["game"] = MakeJsonString(game->GetName().ToStringView());
            call.Reply(std::move(result));
        }

        void HandleRecordStop(ControlCall& call) {
            SessionRecorder& recorder = SessionRecorder::GetInstance();
            if (!recorder.IsRecording()) {
                call.Fail(ControlErrors::kInvalidParams, "no recording is running");
                return;
            }
            Json result = Json::object();
            result["entries"] = recorder.Stop();
            call.Reply(std::move(result));
        }

    }  // namespace

    void RegisterReplayControl(ControlService& service) {
        service.RegisterHandler("record_start", HandleRecordStart);
        service.RegisterHandler("record_stop", HandleRecordStop);

        service.DescribeMethod("record_start", {"Records the accepted reset, perform_action and set_state calls of the active game into a replay file; the game has to be replayable",
                                                {{"path", "string", true, "The file to write on the host the game runs on; an existing file is replaced"},
                                                 {"title", "string", false, "Free text for the header of the file"}}});
        service.DescribeMethod("record_stop", {"Ends the recording; the answer is the number of entries the file holds", {}});
    }

    void StartRecordingFromCommandLine() {
        const CommandLine& commandLine = CoreManager::GetCommandLine();
        const Optional<StringView> path = commandLine.FindValue(kRecordArgument);
        if (!path) {
            if (commandLine.HasFlag(kRecordArgument)) {
                LOG_ERROR("The flag '--record' needs a file: --record=FILE; the game starts without recording");
            }
            return;
        }
        if (path->empty()) {
            LOG_ERROR("The flag '--record=' has no file; the game starts without recording");
            return;
        }

        IGame* game = GameManager::GetInstance().GetActive();
        if (game == nullptr) {
            LOG_ERROR("The flag '--record' needs an active game; the game starts without recording");
            return;
        }
        if (const auto started = SessionRecorder::GetInstance().Start(*path, *game); !started) {
            LOG_ERROR("Cannot record to '{}': {}; the game starts without recording"_format(*path, started.error().message));
            return;
        }
        LOG_INFO("Recording the game '{}' to '{}'"_format(game->GetName().ToStringView(), *path));
    }

}  // namespace BECore
