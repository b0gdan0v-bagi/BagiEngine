#include "ClipboardControl.h"

#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Clipboard/ClipboardAnswers.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    namespace {

        constexpr StringView kTimeoutCode = "clipboard_timeout";

        /// The words of `ToString(ClipboardStatus)` as interned constants
        struct StatusWords {
            static constexpr auto ok = "ok"_intern;
            static constexpr auto empty = "empty"_intern;
            static constexpr auto refused = "refused"_intern;
            static constexpr auto unavailable = "unavailable"_intern;
        };

        PoolString StatusWord(ClipboardStatus status) {
            switch (status) {
                case ClipboardStatus::Ok:
                    return StatusWords::ok;
                case ClipboardStatus::Empty:
                    return StatusWords::empty;
                case ClipboardStatus::Refused:
                    return StatusWords::refused;
                case ClipboardStatus::Unavailable:
                    break;
            }
            return StatusWords::unavailable;
        }

        /// A call that waits, frame by frame, for the clipboard: @c isDone is asked at the end of each frame, @c finish answers
        struct Waiting {
            MoveOnlyFunction<bool()> isDone;
            MoveOnlyFunction<void(ControlCall&)> finish;
            uint32_t waitedFrames = 0;
        };

        /// What a clipboard read leaves for the call that waits for it. Two parties hold it (the callback the clipboard keeps and the call's
        /// wait), both on the main thread
        struct Answer : RefCountedNonAtomic {
            bool arrived = false;
            ClipboardText text;
        };

        void Await(ControlCall& call, UniquePtr<Waiting> waiting) {
            // Frames of zero seconds: on the manual clock nothing moves while the clipboard answers
            call.DeferFrames(1, 0.0, [waiting = std::move(waiting)](ControlCall& done) mutable {
                if (waiting->isDone()) {
                    waiting->finish(done);
                    return;
                }
                if (++waiting->waitedFrames >= kMaxClipboardWaitFrames) {
                    done.Fail(kTimeoutCode, Format("the clipboard did not answer in {} frames", kMaxClipboardWaitFrames));
                    return;
                }
                Await(done, std::move(waiting));
            });
        }

        void HandleSet(ControlCall& call) {
            const auto text = GetParamString(call.ParamsView(), "text");
            if (!text) {
                call.Fail(ControlErrors::kInvalidParams, "'text' has to be a string");
                return;
            }
            ClipboardSetAnswer answer;
            answer.ok = CoreManager::GetClipboard().SetText(*text);
            call.ReplyObject(answer);
        }

        void HandleGet(ControlCall& call) {
            auto answer = New<Answer>();
            CoreManager::GetClipboard().RequestText([answer](const ClipboardText& read) {
                answer->text = read;
                answer->arrived = true;
            });
            auto waiting = MakeUnique<Waiting>();
            waiting->isDone = [answer]() { return answer->arrived; };
            waiting->finish = [answer](ControlCall& done) {
                ClipboardGetAnswer reply;
                reply.status = StatusWord(answer->text.status);
                reply.text = TextView(answer->text.text);
                done.ReplyObject(reply);
            };
            Await(call, std::move(waiting));
        }

        void HandlePaste(ControlCall& call) {
            CoreManager::GetClipboard().RequestPaste();
            auto waiting = MakeUnique<Waiting>();
            waiting->isDone = []() { return !CoreManager::GetClipboard().HasPending(); };
            // An empty answer, like `quit`: the paste itself reaches the game as `PasteEvent`
            waiting->finish = [](ControlCall& done) { done.ReplyEmpty(); };
            Await(call, std::move(waiting));
        }

    }  // namespace

    void RegisterClipboardControl(ControlService& service) {
        service.RegisterHandler("clipboard_set", HandleSet);
        service.RegisterHandler("clipboard_get", HandleGet);
        service.RegisterHandler("clipboard_paste", HandlePaste);

        service.DescribeMethod("clipboard_set", {"Puts text on the system clipboard", {{"text", "string", true, "The text to copy"}}});
        service.DescribeMethod("clipboard_get", {"Reads the system clipboard, answered after some frames", {}});
        service.DescribeMethod("clipboard_paste", {"Delivers a paste to the game as the paste shortcut would", {}});
    }

}  // namespace BECore
