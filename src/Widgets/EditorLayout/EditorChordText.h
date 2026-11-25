#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/PanelDrawSupport.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The chord text of the editor's menus: what `PanelChordText` shows beside a menu item, read from the hotkey map of the registered editor host
     *
     * It installs itself as `PanelChordText::Installed()` when it is made and uninstalls when it ends, so the layout widget owns one as a member and the panel assets find it
     * without knowing the hotkeys. A text is made once per command and kept until `HotkeyMap::Revision` moves (a rebind, a reset, a loaded file), so an open menu allocates
     * nothing in a steady frame. A command with no chord, an unknown one and a missing host give an empty text. Main thread only.
     */
    class EditorChordText final : public PanelChordText {
    public:
        EditorChordText();
        ~EditorChordText() override;

        EditorChordText(const EditorChordText&) = delete;
        EditorChordText& operator=(const EditorChordText&) = delete;

        [[nodiscard]] StringView ChordOf(StringView command) override;

    private:
        struct Entry {
            String command;
            String text;
        };

        Vector<Entry> _entries;
        uint64_t _revision = 0;
        /// The hotkey map the entries were read from; a host made again has another one even when its revision repeats
        const void* _map = nullptr;
    };

}  // namespace BECore
