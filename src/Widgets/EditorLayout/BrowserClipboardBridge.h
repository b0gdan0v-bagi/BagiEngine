#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class Clipboard;
    struct ClipboardText;

    /// The text input of the editor as BrowserClipboardBridge sees it
    class IPasteTarget {
    public:
        virtual ~IPasteTarget() = default;

        /// The field that has the keyboard, 0 when none
        [[nodiscard]] virtual uint32_t GetFocusedField() const = 0;

        /// The number of the current frame
        [[nodiscard]] virtual int GetFrame() const = 0;

        /// Presses the paste chord once more, so the focused field asks for the text again
        virtual void ReplayPaste() = 0;
    };

    /**
     * @brief ImGui's synchronous clipboard over the engine's Clipboard, whose browser read answers later.
     *
     * GetText answers null and asks the clipboard. When an `Ok` text arrives and the field that asked still has the keyboard, the target replays the paste
     * chord, and the GetText of that press gets the text; it expires after kReplayFrames, so a later paste never gets an old text. Any other answer
     * (empty, refused, unavailable), a lost field or a bridge that is gone changes nothing. Presses while a read runs share it, the latest field wins.
     *
     * Main thread only. The target and the clipboard outlive the bridge; the clipboard has no cancel, so a read in flight holds the bridge's state and not
     * the bridge.
     */
    class BrowserClipboardBridge {
    public:
        /// The frames a replayed press has to ask for the text: ImGui spreads its events over several
        static constexpr int kReplayFrames = 8;

        BrowserClipboardBridge(Clipboard& clipboard, IPasteTarget& target);
        ~BrowserClipboardBridge();

        BrowserClipboardBridge(const BrowserClipboardBridge&) = delete;
        BrowserClipboardBridge& operator=(const BrowserClipboardBridge&) = delete;

        /// Copy and cut
        void SetText(StringView text);

        /// Paste: the text during a replayed press, else null. The pointer is valid until the next answer
        [[nodiscard]] const char* GetText();

    private:
        struct State : RefCounted {
            void Answer(const ClipboardText& result);

            IPasteTarget* target = nullptr;  // null once the bridge is gone
            String text;
            uint32_t field = 0;  // the field that asked last
            int answeredFrame = 0;
            bool reading = false;
            bool answered = false;  // text waits for the replayed press
        };

        Clipboard& _clipboard;
        IntrusivePtr<State> _state;
    };

}  // namespace BECore
