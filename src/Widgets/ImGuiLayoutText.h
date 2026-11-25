#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Renames the windows of an ImGui ini text whose names changed, so a layout saved before the change still finds them.
     *
     * ImGui keys the settings of a window by the hash of its name, so a window that was renamed starts floating at its
     * default place, and a dock node that holds it stays empty. The old name of an asset panel is the title before its `###`
     * (the `Config Editor` window is now `Config Editor###ConfigEditor`, whose id is that of `ConfigEditor`; `Sprite Library` is
     * `Sprite Library###SpriteLibrary` the same way).
     *
     * Only `[Window][<name>]` sections change, matched whole and case sensitive; the dock tree (`[Docking][Data]`) stays
     * byte for byte. `Selected=0x...` of a dock node names the active tab by the hash of the old name; ImGui falls back
     * to the first tab of the node, so there is nothing to rewrite. A section of the old name that has a `DockId` is
     * renamed and replaces a section of the new name (what a build after the change wrote for a window it did not
     * know); one without a `DockId` is renamed only when no section of the new name exists. Line ends are kept.
     *
     * @return @p iniText, equal to it when nothing was renamed (the caller can compare)
     */
    [[nodiscard]] String MigrateLegacyWindowNames(StringView iniText);

}  // namespace BECore
