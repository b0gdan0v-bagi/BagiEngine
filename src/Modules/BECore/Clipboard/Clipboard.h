#pragma once

#include <BECore/Clipboard/IClipboardBackend.h>

namespace BECore {

    /**
     * @brief The text clipboard of the game: write, read, and the paste shortcut as an event. `CoreManager::GetClipboard()`.
     *
     * Every read is answered later, from Update() once per frame, never inside the call, because a browser read is asynchronous
     * and one rule is simpler than two. Contract and delivery rules: `Clipboard/CLAUDE.md`. Main thread only.
     */
    class Clipboard {
    public:
        /// Null is no clipboard: IsAvailable() is false and every read is answered with Unavailable. A read in flight is asked again of the new backend
        void SetBackend(IntrusivePtr<IClipboardBackend> backend);

        [[nodiscard]] const IntrusivePtr<IClipboardBackend>& GetBackend() const {
            return _backend;
        }

        [[nodiscard]] bool IsAvailable() const;

        /// @return false when there is no clipboard, the text is longer than kMaxClipboardBytes or the platform refused it
        bool SetText(StringView text);

        /// Asks for the text; @p callback runs from a later Update() with the status and the text
        void RequestText(ClipboardCallback callback);

        /// A RequestText whose Ok answer is emitted as ClipboardEvents::PasteEvent (what the shortcut does); Empty and Refused emit nothing
        void RequestPaste();

        /// Once per frame (CoreManager::OnGameCycle, after the input): the pastes the platform announced, the read in flight, the callbacks
        void Update();

        /// Whether a request waits for its answer
        [[nodiscard]] bool HasPending() const {
            return _readInFlight || !_waiting.empty();
        }

    private:
        void EmitPasted();
        void StartReadIfNeeded();
        void AnswerAll(const ClipboardText& result);

        IntrusivePtr<IClipboardBackend> _backend;
        Vector<ClipboardCallback> _waiting;    ///< asked, not yet in a backend read; also the ones that arrive while a read runs
        Vector<ClipboardCallback> _reading;    ///< the requests the read in flight will answer
        Vector<ClipboardCallback> _answering;  ///< being called right now; a callback that asks again goes to _waiting
        ClipboardText _result;
        ClipboardText _pasted;
        bool _readInFlight = false;
    };

}  // namespace BECore
