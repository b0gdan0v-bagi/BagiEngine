#pragma once

namespace BECore {

    /// The longest text the clipboard service moves in either direction: a longer one is Refused, so a hostile clipboard cannot make the game copy megabytes
    inline constexpr size_t kMaxClipboardBytes = 1024 * 1024;

    /// The outcome of a clipboard call; none of them is an assert or a crash.
    /// Ok: the text is there (a write: the platform took it). Empty: the clipboard holds no text. Refused: the platform or the user said no, or the text is longer than
    /// kMaxClipboardBytes. Unavailable: no clipboard API at all (no backend, a page over plain http://).
    CORE_ENUM(ClipboardStatus, uint8_t, Ok, Empty, Refused, Unavailable)

    /// `ok`, `empty`, `refused` or `unavailable`: the word of the control API
    constexpr StringView ToString(ClipboardStatus status) {
        switch (status) {
            case ClipboardStatus::Ok:
                return "ok";
            case ClipboardStatus::Empty:
                return "empty";
            case ClipboardStatus::Refused:
                return "refused";
            case ClipboardStatus::Unavailable:
                return "unavailable";
        }
        return "unavailable";
    }

    /// The answer to a read; @c text is not empty only with Ok
    struct ClipboardText {
        ClipboardStatus status = ClipboardStatus::Empty;
        eastl::string text;
    };

    /// Called by Clipboard::Update(), never inside the request
    using ClipboardCallback = eastl::function<void(const ClipboardText&)>;

}  // namespace BECore
