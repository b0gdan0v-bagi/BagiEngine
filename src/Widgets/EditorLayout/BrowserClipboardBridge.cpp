#include "BrowserClipboardBridge.h"

#include <BECore/Clipboard/Clipboard.h>

namespace BECore {

    BrowserClipboardBridge::BrowserClipboardBridge(Clipboard& clipboard, IPasteTarget& target) : _clipboard(clipboard), _state(New<State>()) {
        _state->target = &target;
    }

    BrowserClipboardBridge::~BrowserClipboardBridge() {
        _state->target = nullptr;
    }

    void BrowserClipboardBridge::SetText(StringView text) {
        // A refusal is the clipboard's own warning; ImGui has no answer to give it to
        _clipboard.SetText(text);
    }

    const char* BrowserClipboardBridge::GetText() {
        State& state = *_state;
        const uint32_t field = state.target->GetFocusedField();
        if (state.answered) {
            state.answered = false;
            if (state.field == field && state.target->GetFrame() - state.answeredFrame <= kReplayFrames) {
                return state.text.c_str();
            }
        }
        state.field = field;
        if (!state.reading) {
            state.reading = true;
            _clipboard.RequestText([held = _state](const ClipboardText& result) { held->Answer(result); });
        }
        return nullptr;
    }

    void BrowserClipboardBridge::State::Answer(const ClipboardText& result) {
        reading = false;
        if (target == nullptr || result.status != ClipboardStatus::Ok || field == 0 || target->GetFocusedField() != field) {
            return;
        }
        text = result.text;
        answered = true;
        answeredFrame = target->GetFrame();
        target->ReplayPaste();
    }

}  // namespace BECore
