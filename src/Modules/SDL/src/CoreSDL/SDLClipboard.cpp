#include "SDLClipboard.h"

#include <SDL3/SDL.h>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/em_js.h>

// The JavaScript of the browser backend calls these runtime functions; EM_ASM code does not pull them in by itself
EM_JS_DEPS(bagi_clipboard, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");
#endif

namespace BECore {

    namespace {

#if defined(__APPLE__)
        constexpr SDL_Keymod kPrimaryModifier = SDL_KMOD_GUI;
        constexpr SDL_Keymod kOtherPrimaryModifier = SDL_KMOD_CTRL;
#elif defined(__EMSCRIPTEN__)
        // The page does not know its platform: Cmd on a Mac and Ctrl elsewhere both paste, and the key handler of the page takes both
        constexpr SDL_Keymod kPrimaryModifier = SDL_KMOD_CTRL | SDL_KMOD_GUI;
        constexpr SDL_Keymod kOtherPrimaryModifier = SDL_KMOD_NONE;
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

    bool SDLClipboardBackend::WriteText(StringView text) {
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

    ClipboardStatus StatusFromWebCode(int code) {
        switch (code) {
            case kWebCodeOk:
                return ClipboardStatus::Ok;
            case kWebCodeEmpty:
                return ClipboardStatus::Empty;
            case kWebCodeUnavailable:
                return ClipboardStatus::Unavailable;
            default:
                return ClipboardStatus::Refused;  // kWebCodeRefused and a code nobody writes
        }
    }

    bool IsGestureReadFresh(double nowMs, double startedMs, double windowMs) {
        if (!std::isfinite(nowMs) || !std::isfinite(startedMs) || !std::isfinite(windowMs)) {
            return false;
        }
        return startedMs <= nowMs && nowMs - startedMs <= windowMs;
    }

    ClipboardText MakeWebClipboardText(int code, StringView text) {
        ClipboardText result;
        result.status = StatusFromWebCode(code);
        if (result.status == ClipboardStatus::Ok) {
            result.text.assign(text.data(), text.size());
        }
        return result;
    }

#ifdef __EMSCRIPTEN__
    namespace {

        // A read the page never finishes (a permission prompt nobody answers) becomes Refused after this long
        constexpr double kReadTimeoutMs = 60000.0;
        // A write the browser refused outside a gesture is tried again at this many gestures, then dropped
        constexpr int kWriteRetryGestures = 3;
        // Pastes of the page the game has not taken yet; the oldest is dropped beyond it
        constexpr int kPastedQueueLimit = 8;
        // What PollRead sees while the read has not finished
        constexpr int kWebCodePending = -1;
        // What a write reports: the browser has no API, the call threw at once, the call was made (it can still be rejected later)
        constexpr int kWriteNoApi = -1;
        constexpr int kWriteThrew = 0;

        // The JavaScript of EM_ASM is stringized as it stands: keep it between clang-format off/on (the formatter splits ===) and put
        // no comma outside parentheses (the macro splits its arguments there; braces and brackets do not protect it)
        // The state of the page lives in window.bagiClipboard (as bagiSafeAreaProbe does), made once:
        //   read         the read BeginRead started: done, code, text, startedMs
        //   gestureRead  the read the keydown of Ctrl/Cmd+V started inside the gesture, for BeginRead to take if it is fresh
        //   pendingWrite a text the browser refused, tried again at the next gesture (Safari writes only inside one)
        //   pasted       the texts of the paste event of the page, taken by TakePasted
        //   outgoing     the string being copied to the C++ side
        // clang-format off
        void InstallPageState() {
            EM_ASM({
                if (!window.bagiClipboard) {
                    var s = {};
                    window.bagiClipboard = s;
                    s.codeOk = $0;
                    s.codeEmpty = $1;
                    s.codeRefused = $2;
                    s.codeUnavailable = $3;
                    s.maxRetries = $4;
                    s.pastedLimit = $5;
                    s.noApi = $6;
                    s.threw = $7;
                    s.read = null;
                    s.gestureRead = null;
                    s.pendingWrite = null;
                    s.pasted = [];
                    s.outgoing = "";
                    s.writeId = 0;
                    s.retries = 0;
                    s.warned = false;

                    s.startRead = function() {
                        var r = {};
                        r.done = false;
                        r.code = s.codeRefused;
                        r.text = "";
                        r.startedMs = performance.now();
                        var api = navigator.clipboard;
                        if (!api || !api.readText) {
                            r.done = true;
                            r.code = s.codeUnavailable;
                            return r;
                        }
                        var finish = function(code, text) {
                            r.code = code;
                            r.text = text;
                            r.done = true;
                        };
                        var accepted = function(text) {
                            var value = typeof text === "string" ? text : "";
                            finish(value.length > 0 ? s.codeOk : s.codeEmpty, value);
                        };
                        var rejected = function() {
                            finish(s.codeRefused, "");
                        };
                        try {
                            api.readText().then(accepted, rejected);
                        } catch (e) {
                            rejected();
                        }
                        return r;
                    };

                    s.attemptWrite = function(text) {
                        var api = navigator.clipboard;
                        if (!api || !api.writeText) {
                            return s.noApi;
                        }
                        var id = s.writeId;
                        var keep = function() {
                            if (id === s.writeId) {
                                s.pendingWrite = text;
                                if (!s.warned) {
                                    s.warned = true;
                                    console.warn("Clipboard: the browser refused the write, it is tried again at the next tap or key press");
                                }
                            }
                        };
                        try {
                            api.writeText(text).then(null, keep);
                        } catch (e) {
                            keep();
                            return s.threw;
                        }
                        return 1;
                    };

                    s.write = function(text) {
                        s.writeId = s.writeId + 1;
                        s.pendingWrite = null;
                        s.retries = 0;
                        s.gestureRead = null;
                        return s.attemptWrite(text);
                    };

                    s.retryWrite = function() {
                        var text = s.pendingWrite;
                        if (text === null) {
                            return;
                        }
                        s.pendingWrite = null;
                        s.retries = s.retries + 1;
                        if (s.retries <= s.maxRetries) {
                            s.attemptWrite(text);
                        }
                    };

                    var onGesture = function() {
                        s.retryWrite();
                    };
                    window.addEventListener("pointerup", onGesture, true);
                    window.addEventListener("touchend", onGesture, true);
                    window.addEventListener("keydown", function(e) {
                        s.retryWrite();
                        if ((e.ctrlKey || e.metaKey) && !e.altKey && !e.repeat && typeof e.key === "string" && e.key.toLowerCase() === "v") {
                            s.gestureRead = s.startRead();
                        }
                    }, true);
                    window.addEventListener("paste", function(e) {
                        var data = e.clipboardData;
                        var text = data ? data.getData("text/plain") : "";
                        s.pasted.push(typeof text === "string" ? text : "");
                        if (s.pasted.length > s.pastedLimit) {
                            s.pasted.shift();
                        }
                    }, true);
                }
            }, kWebCodeOk, kWebCodeEmpty, kWebCodeRefused, kWebCodeUnavailable, kWriteRetryGestures, kPastedQueueLimit, kWriteNoApi, kWriteThrew);
        }

        bool PageHasClipboardApi() {
            return EM_ASM_INT({
                var api = navigator.clipboard;
                return api && (api.writeText || api.readText) ? 1 : 0;
            }) != 0;
        }

        // Writes at once; the result is kWriteNoApi, kWriteThrew or 1
        int WriteToPage(StringView text) {
            return EM_ASM_INT({ return window.bagiClipboard.write(UTF8ToString($0, $1)); }, text.data(), static_cast<int>(text.size()));
        }

        double PageNowMs() {
            return EM_ASM_DOUBLE({ return performance.now(); });
        }

        // When the read of the last Ctrl/Cmd+V started (NaN when there is none)
        double GestureReadStartedMs() {
            return EM_ASM_DOUBLE({
                var s = window.bagiClipboard;
                return s.gestureRead ? s.gestureRead.startedMs : NaN;
            });
        }

        // The read the game asks for is the gesture read when that is fresh, else a read made now. The gesture read is used up either way.
        void StartPageRead(bool useGestureRead) {
            EM_ASM({
                var s = window.bagiClipboard;
                s.read = $0 && s.gestureRead ? s.gestureRead : s.startRead();
                s.gestureRead = null;
            }, useGestureRead ? 1 : 0);
        }

        // The code of the finished read, kWebCodePending while it runs; a read that waits too long or is lost is Refused
        int PollPageRead() {
            return EM_ASM_INT({
                var s = window.bagiClipboard;
                var r = s.read;
                if (!r) {
                    return $1;
                }
                if (!r.done && performance.now() - r.startedMs > $0) {
                    r.done = true;
                    r.code = $1;
                    r.text = "";
                }
                return r.done ? r.code : $2;
            }, kReadTimeoutMs, kWebCodeRefused, kWebCodePending);
        }

        // Puts the text of the finished read into `outgoing` and forgets the read
        void TakePageRead() {
            EM_ASM({
                var s = window.bagiClipboard;
                s.outgoing = s.read ? s.read.text : "";
                s.read = null;
            });
        }

        // Moves the oldest paste of the page into `outgoing`; false when there is none
        bool TakePagePaste() {
            return EM_ASM_INT({
                var s = window.bagiClipboard;
                if (s.pasted.length === 0) {
                    return 0;
                }
                s.outgoing = s.pasted.shift();
                return 1;
            }) != 0;
        }

        // Copies `outgoing` into @p out in two steps (the size, then the bytes into a buffer sized here), so no function is exported.
        // @return false, with @p out empty, when the text is longer than kMaxClipboardBytes
        bool CopyOutgoing(String& out) {
            out.clear();
            const int bytes = EM_ASM_INT({ return lengthBytesUTF8(window.bagiClipboard.outgoing); });
            const bool fits = bytes >= 0 && static_cast<size_t>(bytes) <= kMaxClipboardBytes;
            if (fits && bytes > 0) {
                out.resize(static_cast<size_t>(bytes) + 1);  // stringToUTF8 counts the terminator in its limit
                EM_ASM({ stringToUTF8(window.bagiClipboard.outgoing, $0, $1); }, out.data(), bytes + 1);
                out.resize(static_cast<size_t>(bytes));
            }
            EM_ASM({ window.bagiClipboard.outgoing = ""; });
            return fits;
        }
        // clang-format on

        /// The browser clipboard: navigator.clipboard, the keydown of Ctrl/Cmd+V (a read must start inside the gesture) and the paste event of the page
        class WebClipboardBackend : public IClipboardBackend {
        public:
            WebClipboardBackend() {
                InstallPageState();
                _available = PageHasClipboardApi();
                if (!_available) {
                    LOG_WARNING("Clipboard: the page has no navigator.clipboard (plain http:// from another host?): copy and paste are unavailable");
                }
            }

            [[nodiscard]] bool IsAvailable() const override {
                return _available;
            }

            bool WriteText(StringView text) override {
                const int result = WriteToPage(text);
                if (result == kWriteThrew && !std::exchange(_warnedWrite, true)) {
                    LOG_WARNING("Clipboard: navigator.clipboard.writeText threw, the text is tried again at the next tap or key press");
                }
                return result != kWriteNoApi;
            }

            void BeginRead() override {
                const bool fresh = IsGestureReadFresh(PageNowMs(), GestureReadStartedMs(), kGestureReadWindowMs);
                StartPageRead(fresh);
                _reading = true;
            }

            bool PollRead(ClipboardText& out) override {
                if (!_reading) {
                    return false;
                }
                int code = PollPageRead();
                if (code == kWebCodePending) {
                    return false;
                }
                _reading = false;
                TakePageRead();
                if (!CopyOutgoing(_text)) {
                    code = kWebCodeRefused;
                }
                if (code == kWebCodeRefused && !std::exchange(_warnedRead, true)) {
                    LOG_WARNING("Clipboard: the browser refused the read (permission denied, or not made inside a tap or key press)");
                }
                out = MakeWebClipboardText(code, _text);
                return true;
            }

            bool TakePasted(ClipboardText& out) override {
                if (!TakePagePaste()) {
                    return false;
                }
                int code = kWebCodeOk;
                if (!CopyOutgoing(_text)) {
                    code = kWebCodeRefused;
                } else if (_text.empty()) {
                    code = kWebCodeEmpty;
                }
                out = MakeWebClipboardText(code, _text);
                return true;
            }

        private:
            bool _available = false;
            bool _reading = false;
            bool _warnedWrite = false;
            bool _warnedRead = false;
            String _text;  ///< the text being moved from the page, kept so a read does not allocate for the same size again
        };

        /// Installs the page state once (the listeners are added a single time per page) and returns the backend over it
        IntrusivePtr<IClipboardBackend> InstallWebClipboard() {
            return IntrusivePtr<IClipboardBackend>(New<WebClipboardBackend>());
        }

    }  // namespace
#endif

    IntrusivePtr<IClipboardBackend> MakePlatformClipboardBackend() {
#ifdef __EMSCRIPTEN__
        return InstallWebClipboard();
#else
        return IntrusivePtr<IClipboardBackend>(New<SDLClipboardBackend>());
#endif
    }

}  // namespace BECore
