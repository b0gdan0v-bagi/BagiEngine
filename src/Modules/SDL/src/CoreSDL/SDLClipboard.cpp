#include "SDLClipboard.h"

#include <SDL3/SDL.h>

namespace BECore {

    namespace {

#if defined(__APPLE__)
        constexpr SDL_Keymod kPrimaryModifier = SDL_KMOD_GUI;
        constexpr SDL_Keymod kOtherPrimaryModifier = SDL_KMOD_CTRL;
#else
        constexpr SDL_Keymod kPrimaryModifier = SDL_KMOD_CTRL;
        constexpr SDL_Keymod kOtherPrimaryModifier = SDL_KMOD_GUI;
#endif

    }  // namespace

    bool IsPasteShortcut(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
        if (key != SDLK_V || repeat) {
            return false;
        }
        return (mod & kPrimaryModifier) != 0 && (mod & (kOtherPrimaryModifier | SDL_KMOD_ALT)) == 0;
    }

    bool SDLClipboardBackend::IsAvailable() const {
        return (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0;
    }

    bool SDLClipboardBackend::WriteText(eastl::string_view text) {
        _writeBuffer.assign(text.data(), text.size());
        if (!SDL_SetClipboardText(_writeBuffer.c_str())) {
            LOG_WARNING("Clipboard: SDL_SetClipboardText failed: {}"_format(SDL_GetError()));
            return false;
        }
        return true;
    }

    void SDLClipboardBackend::BeginRead() {}

    bool SDLClipboardBackend::PollRead(ClipboardText& out) {
        out.text.clear();
        out.status = ClipboardStatus::Empty;
        if (!SDL_HasClipboardText()) {
            return true;
        }
        char* text = SDL_GetClipboardText();  // an empty string when it fails or there is none
        if (text != nullptr) {
            if (text[0] != '\0') {
                out.status = ClipboardStatus::Ok;
                out.text = text;
            }
            SDL_free(text);
        }
        return true;
    }

    IntrusivePtr<IClipboardBackend> MakePlatformClipboardBackend() {
        return IntrusivePtr<IClipboardBackend>(New<SDLClipboardBackend>());
    }

}  // namespace BECore
