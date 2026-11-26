#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// What the editor runs in. A browser page cannot start a process, watch the files of a project or hand the user a file it wrote
    CORE_ENUM(EditorHostKind, uint8_t, Desktop, Browser)

    /// The host of this build (`Browser` under Emscripten) unless SetEditorHost chose another
    [[nodiscard]] EditorHostKind GetEditorHost();

    /// For the tests of the browser editor on a desktop. Set it before the panels are made: they ask once, when their asset is prepared
    void SetEditorHost(EditorHostKind host);

    /**
     * @brief Whether the host can do what @p commandId asks for; an unavailable command is not offered and its panel is not hosted.
     *
     * The command table stays whole in every host, because the shipped panel assets name these ids and their validator has to know them.
     * An empty or unknown id counts as available.
     */
    [[nodiscard]] bool IsEditorCommandAvailable(StringView commandId);

    /// Whether the notice that a browser editor keeps nothing is still due
    class ReloadNotice {
    public:
        /// True for the first call with the browser host; false on every later one, and on a desktop (which leaves it due)
        [[nodiscard]] bool Take(EditorHostKind host);

    private:
        bool _taken = false;
    };

    /// The notice of the process (a page load): a mode switch makes the widgets again and must not show it again
    [[nodiscard]] ReloadNotice& GetReloadNotice();

}  // namespace BECore
