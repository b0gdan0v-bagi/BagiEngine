#pragma once

#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/BrowserClipboardBridge.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <imgui.h>

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /// The characters of the keys whose browser action the editor takes over while Ctrl or Cmd is held: its own and those of the default hotkeys of @p commands
    [[nodiscard]] String EditorBrowserKeys(Span<const EditorCommand> commands);

    /// Whether the editor's page runs on a Mac or an Apple phone; false outside a browser
    [[nodiscard]] bool IsMacBrowserPage();

    /// Releases every key but the modifiers in ImGui: a browser on a Mac tells no release of a key let go while Cmd is held
    void ReleaseKeysHeldUnderCommand(ImGuiIO& io);

    /**
     * @brief The browser's input for the editor, installed for the life of the object: made by the layout widget in the browser host, so Game mode keeps the
     * game's input as it was.
     *
     * - A capture-phase `keydown` listener of the page calls `preventDefault` for Ctrl or Cmd with a key of EditorBrowserKeys (not with Alt: AltGr types
     *   characters). The event still reaches SDL. C, V and X are never taken.
     * - On a Mac page ImGui reads Cmd as Ctrl (`ConfigMacOSXBehaviors`).
     * - ImGui's clipboard goes through BrowserClipboardBridge.
     *
     * Needs an ImGui context with its platform backend set up, which must outlive the object.
     */
    class EditorBrowserInput : public IPasteTarget, public SubscriptionHolder {
    public:
        EditorBrowserInput(Clipboard& clipboard, bool macPage);
        /// Puts ImGui's clipboard and Mac behaviours back and removes the page's listener
        ~EditorBrowserInput() override;

        EditorBrowserInput(const EditorBrowserInput&) = delete;
        EditorBrowserInput& operator=(const EditorBrowserInput&) = delete;

        [[nodiscard]] uint32_t GetFocusedField() const override;
        [[nodiscard]] int GetFrame() const override;
        void ReplayPaste() override;

    private:
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);

        BrowserClipboardBridge _bridge;
        const char* (*_previousGetText)(ImGuiContext*) = nullptr;
        void (*_previousSetText)(ImGuiContext*, const char*) = nullptr;
        void* _previousClipboardData = nullptr;
        bool _previousMacBehaviors = false;
    };

}  // namespace BECore
