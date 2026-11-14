#include "ClipboardControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    namespace {

        constexpr StringView kTimeoutCode = "clipboard_timeout";

        /// A call that waits, frame by frame, for the clipboard: @c isDone is asked at the end of each frame, @c finish answers
        struct Waiting {
            std::function<bool()> isDone;
            std::function<void(ControlCall&)> finish;
            uint32_t waitedFrames = 0;
        };

        void Await(ControlCall& call, eastl::shared_ptr<Waiting> waiting) {
            // Frames of zero seconds: on the manual clock nothing moves while the clipboard answers
            call.DeferFrames(1, 0.0, [waiting](ControlCall& done) {
                if (waiting->isDone()) {
                    waiting->finish(done);
                    return;
                }
                if (++waiting->waitedFrames >= kMaxClipboardWaitFrames) {
                    done.Fail(kTimeoutCode, Format("the clipboard did not answer in {} frames", kMaxClipboardWaitFrames));
                    return;
                }
                Await(done, waiting);
            });
        }

        void HandleSet(ControlCall& call) {
            const auto text = GetJsonString(call.Params(), "text");
            if (!text) {
                call.Fail(ControlErrors::kInvalidParams, "'text' has to be a string");
                return;
            }
            Json result = Json::object();
            result["ok"] = CoreManager::GetClipboard().SetText(*text);
            call.Reply(std::move(result));
        }

        void HandleGet(ControlCall& call) {
            struct Answer {
                bool arrived = false;
                ClipboardText text;
            };
            auto answer = eastl::make_shared<Answer>();
            CoreManager::GetClipboard().RequestText([answer](const ClipboardText& read) {
                answer->text = read;
                answer->arrived = true;
            });
            auto waiting = eastl::make_shared<Waiting>();
            waiting->isDone = [answer]() { return answer->arrived; };
            waiting->finish = [answer](ControlCall& done) {
                Json result = Json::object();
                result["status"] = MakeJsonString(ToString(answer->text.status));
                result["text"] = MakeJsonString(answer->text.text);
                done.Reply(std::move(result));
            };
            Await(call, std::move(waiting));
        }

        void HandlePaste(ControlCall& call) {
            CoreManager::GetClipboard().RequestPaste();
            auto waiting = eastl::make_shared<Waiting>();
            waiting->isDone = []() { return !CoreManager::GetClipboard().HasPending(); };
            waiting->finish = [](ControlCall& done) { done.Reply(Json::object()); };
            Await(call, std::move(waiting));
        }

    }  // namespace

    void RegisterClipboardControl(ControlService& service) {
        service.RegisterHandler("clipboard_set", HandleSet);
        service.RegisterHandler("clipboard_get", HandleGet);
        service.RegisterHandler("clipboard_paste", HandlePaste);
    }

}  // namespace BECore
