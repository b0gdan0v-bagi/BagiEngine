#pragma once

#include <BECore/Clipboard/IClipboardBackend.h>
#include <SDL3/SDL_keycode.h>

namespace BECore {

    /**
     * @brief Whether a key press is the paste shortcut: V with Cmd on Apple platforms, with Ctrl elsewhere.
     *
     * Shift is allowed (Ctrl+Shift+V pastes too); Alt and the other primary modifier are not (Win+V is the clipboard history of
     * Windows, Ctrl+V on a Mac is not paste), and a key repeat is not a new shortcut. Pure, so it runs, and is tested, on every platform.
     * In a browser build (no platform to ask) either of Cmd and Ctrl is the primary modifier, as the key handler of the page takes it.
     */
    bool IsPasteShortcut(SDL_Keycode key, SDL_Keymod mod, bool repeat);

    /// The integers the JavaScript of the browser backend writes for the outcome of a read (`StatusFromWebCode`)
    inline constexpr int kWebCodeOk = 0;
    inline constexpr int kWebCodeEmpty = 1;
    inline constexpr int kWebCodeRefused = 2;
    inline constexpr int kWebCodeUnavailable = 3;

    /// How long (ms) a read started by Ctrl/Cmd+V inside the key press may wait for the game to ask for it; a browser allows
    /// `readText` only inside a gesture (Safari does not take one made a frame later), so the page starts the read itself
    inline constexpr double kGestureReadWindowMs = 2000.0;

    /// The status of a web code; a code that is none of the four is Refused
    ClipboardStatus StatusFromWebCode(int code);

    /// Whether a read started at @p startedMs is still good at @p nowMs: not in the future, not older than @p windowMs, no NaN.
    /// A read exactly @p windowMs old is fresh.
    bool IsGestureReadFresh(double nowMs, double startedMs, double windowMs);

    /// The answer for a code and a text; the text is kept only for Ok
    ClipboardText MakeWebClipboardText(int code, StringView text);

    /// The desktop clipboard: SDL_SetClipboardText / SDL_GetClipboardText. A read finishes at once (PollRead is true on the first call)
    class SDLClipboardBackend : public IClipboardBackend {
    public:
        /// True once SDL video is initialised: before that SDL has no clipboard
        [[nodiscard]] bool IsAvailable() const override;

        bool WriteText(StringView text) override;
        void BeginRead() override;
        bool PollRead(ClipboardText& out) override;

    private:
        String _writeBuffer;  ///< SDL wants a terminated string; kept so a write does not allocate again for the same size
    };

    /// The clipboard backend of this platform: the SDL one on the desktop, the page's navigator.clipboard in the browser
    IntrusivePtr<IClipboardBackend> MakePlatformClipboardBackend();

}  // namespace BECore
