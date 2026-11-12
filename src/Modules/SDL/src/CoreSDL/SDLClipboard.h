#pragma once

#include <BECore/Clipboard/IClipboardBackend.h>
#include <SDL3/SDL_keycode.h>

namespace BECore {

    /**
     * @brief Whether a key press is the paste shortcut: V with Cmd on Apple platforms, with Ctrl elsewhere.
     *
     * Shift is allowed (Ctrl+Shift+V pastes too); Alt and the other primary modifier are not (Win+V is the clipboard history of
     * Windows, Ctrl+V on a Mac is not paste), and a key repeat is not a new shortcut. Pure, so it runs, and is tested, on every platform.
     */
    bool IsPasteShortcut(SDL_Keycode key, SDL_Keymod mod, bool repeat);

    /// The desktop clipboard: SDL_SetClipboardText / SDL_GetClipboardText. A read finishes at once (PollRead is true on the first call)
    class SDLClipboardBackend : public IClipboardBackend {
    public:
        /// True once SDL video is initialised: before that SDL has no clipboard
        [[nodiscard]] bool IsAvailable() const override;

        bool WriteText(eastl::string_view text) override;
        void BeginRead() override;
        bool PollRead(ClipboardText& out) override;

    private:
        eastl::string _writeBuffer;  ///< SDL wants a terminated string; kept so a write does not allocate again for the same size
    };

    /// The clipboard backend of this platform: the SDL one on the desktop
    IntrusivePtr<IClipboardBackend> MakePlatformClipboardBackend();

}  // namespace BECore
