#include "EditorBrowserInput.h"

#include <CoreSDL/SDLEvents.h>
#include <SDL3/SDL_keyboard.h>
#include <imgui_internal.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/em_js.h>

// The listener below calls this runtime function; EM_ASM code does not pull it in by itself
EM_JS_DEPS(bagi_editor_input, "$UTF8ToString");
#endif

namespace BECore {

    namespace {

        /// Taken whatever the hotkey table holds: save, the palette and preferences, and the hierarchy panel's search and duplicate
        constexpr StringView kOwnBrowserKeys = "spfd,";

        /// The character a browser reports for @p key, 0 for a key the page is not asked to keep
        char BrowserCharacter(ImGuiKey key) {
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                return static_cast<char>('a' + (key - ImGuiKey_A));
            }
            if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
                return static_cast<char>('0' + (key - ImGuiKey_0));
            }
            switch (key) {
                case ImGuiKey_Comma:
                    return ',';
                case ImGuiKey_Period:
                    return '.';
                case ImGuiKey_Slash:
                    return '/';
                default:
                    return 0;
            }
        }

        /// Gives ImGui the Ctrl+Tab chords of its window list, which it works out only when the context is made
        void SetWindowingChords(ImGuiContext& context) {
            const ImGuiKeyChord modifier = context.IO.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            context.ConfigNavWindowingKeyNext = modifier | ImGuiKey_Tab;
            context.ConfigNavWindowingKeyPrev = modifier | ImGuiMod_Shift | ImGuiKey_Tab;
        }

#ifdef __EMSCRIPTEN__
        // The JavaScript of EM_ASM is stringized as it stands: keep it between clang-format off/on and put no comma outside parentheses.
        // A layout that types no Latin letter is matched by the place of the key (e.code)
        // clang-format off
        bool PageNamesApplePlatform() {
            return EM_ASM_INT({ return /Mac|iPhone|iPad/.test(navigator.platform) ? 1 : 0; }) != 0;
        }

        void InstallKeyListener(const String& keys) {
            EM_ASM({
                var keys = UTF8ToString($0);
                window.bagiEditorKeys = function(e) {
                    if (!(e.ctrlKey || e.metaKey) || e.altKey || typeof e.key !== "string") {
                        return;
                    }
                    var key = e.key.toLowerCase();
                    if (key.length === 1 && key.charCodeAt(0) > 127) {
                        key = e.code && e.code.indexOf("Key") === 0 ? e.code.charAt(3).toLowerCase() : "";
                    }
                    if (key.length === 1 && keys.indexOf(key) >= 0) {
                        e.preventDefault();
                    }
                };
                window.addEventListener("keydown", window.bagiEditorKeys, true);
            }, keys.c_str());
        }

        void RemoveKeyListener() {
            EM_ASM({
                window.removeEventListener("keydown", window.bagiEditorKeys, true);
                delete window.bagiEditorKeys;
            });
        }
        // clang-format on
#endif

    }  // namespace

    String EditorBrowserKeys(Span<const EditorCommand> commands) {
        String keys(kOwnBrowserKeys.data(), kOwnBrowserKeys.size());
        for (const EditorCommand& command : commands) {
            if (!command.defaultChord || (command.defaultChord->mods & kChordModCtrl) == 0) {
                continue;
            }
            const char key = BrowserCharacter(command.defaultChord->key);
            if (key != 0 && keys.find(key) == String::npos) {
                keys.push_back(key);
            }
        }
        return keys;
    }

    bool IsMacBrowserPage() {
#ifdef __EMSCRIPTEN__
        return PageNamesApplePlatform();
#else
        return false;
#endif
    }

    void ReleaseKeysHeldUnderCommand(ImGuiIO& io) {
        for (int key = ImGuiKey_Tab; key <= ImGuiKey_KeypadEqual; ++key) {
            if (key < ImGuiKey_LeftCtrl || key > ImGuiKey_RightSuper) {
                // ImGui drops the release of a key that is up already
                io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
            }
        }
    }

    EditorBrowserInput::EditorBrowserInput(Clipboard& clipboard, bool macPage) : _bridge(clipboard, *this) {
        ImGuiContext& context = *ImGui::GetCurrentContext();
        _previousMacBehaviors = context.IO.ConfigMacOSXBehaviors;
        context.IO.ConfigMacOSXBehaviors = macPage;
        SetWindowingChords(context);

        ImGuiPlatformIO& platform = context.PlatformIO;
        _previousGetText = platform.Platform_GetClipboardTextFn;
        _previousSetText = platform.Platform_SetClipboardTextFn;
        _previousClipboardData = platform.Platform_ClipboardUserData;
        platform.Platform_ClipboardUserData = this;
        platform.Platform_GetClipboardTextFn = [](ImGuiContext* imgui) { return static_cast<EditorBrowserInput*>(imgui->PlatformIO.Platform_ClipboardUserData)->_bridge.GetText(); };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext* imgui, const char* text) { static_cast<EditorBrowserInput*>(imgui->PlatformIO.Platform_ClipboardUserData)->_bridge.SetText(text); };

        Subscribe<SDLEvents::SDLEventWrapper, &EditorBrowserInput::OnSDLEvent>(this);
#ifdef __EMSCRIPTEN__
        InstallKeyListener(EditorBrowserKeys(GetEditorCommands()));
#endif
    }

    EditorBrowserInput::~EditorBrowserInput() {
#ifdef __EMSCRIPTEN__
        RemoveKeyListener();
#endif
        ImGuiContext& context = *ImGui::GetCurrentContext();
        context.PlatformIO.Platform_GetClipboardTextFn = _previousGetText;
        context.PlatformIO.Platform_SetClipboardTextFn = _previousSetText;
        context.PlatformIO.Platform_ClipboardUserData = _previousClipboardData;
        context.IO.ConfigMacOSXBehaviors = _previousMacBehaviors;
        SetWindowingChords(context);
    }

    uint32_t EditorBrowserInput::GetFocusedField() const {
        return ImGui::GetActiveID();
    }

    int EditorBrowserInput::GetFrame() const {
        return ImGui::GetFrameCount();
    }

    void EditorBrowserInput::ReplayPaste() {
        ImGuiIO& io = ImGui::GetIO();
        // AddKeyEvent swaps Ctrl and Super on a Mac page, so the page's own paste modifier is passed
        const ImGuiKey modifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
        // The keys of the press that asked may still be held, and ImGui sees a new press only after a release
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        io.AddKeyEvent(ImGuiKey_V, false);
        io.AddKeyEvent(modifier, true);
        io.AddKeyEvent(ImGuiKey_V, true);
        io.AddKeyEvent(ImGuiKey_V, false);
        io.AddKeyEvent(modifier, false);
        // The modifiers end as the keyboard holds them, as the SDL backend reports them
        const SDL_Keymod held = SDL_GetModState();
        io.AddKeyEvent(ImGuiMod_Ctrl, (held & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (held & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (held & SDL_KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (held & SDL_KMOD_GUI) != 0);
    }

    void EditorBrowserInput::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) {
        ImGuiIO& io = ImGui::GetIO();
        const SDL_Event& raw = event.event;
        // The layout widget subscribes after ImGuiWidget, so the release of Cmd is queued in ImGui before these
        if (io.ConfigMacOSXBehaviors && raw.type == SDL_EVENT_KEY_UP && (raw.key.key == SDLK_LGUI || raw.key.key == SDLK_RGUI)) {
            ReleaseKeysHeldUnderCommand(io);
        }
    }

}  // namespace BECore
