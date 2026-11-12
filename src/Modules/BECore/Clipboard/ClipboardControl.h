#pragma once

namespace BECore {

    class ControlService;

    /// Frames a `clipboard_get` or `clipboard_paste` waits for the clipboard before it fails with `clipboard_timeout`: ten seconds at 60 fps
    inline constexpr uint32_t kMaxClipboardWaitFrames = 600;

    /**
     * @brief Registers the clipboard methods of the control API, so a test script can fill and read the clipboard and simulate the paste shortcut.
     *
     * - `clipboard_set {"text": string}` answers `{"ok": bool}`, what `Clipboard::SetText` returned; a missing or non-string `text` is `invalid_params`.
     * - `clipboard_get` answers `{"status": "ok" / "empty" / "refused" / "unavailable", "text": string}` once the read has run (frames pass); `clipboard_timeout` after
     *   kMaxClipboardWaitFrames.
     * - `clipboard_paste` answers `{}` once `Clipboard::RequestPaste()` is delivered, so a `PasteEvent` has reached the game exactly as the shortcut gives it.
     *
     * The reads wait for frames (the clipboard answers from `Clipboard::Update()`), so they work with the manual clock too: the frames they wait for last zero seconds.
     * Registering twice replaces the handlers.
     */
    void RegisterClipboardControl(ControlService& service);

}  // namespace BECore
