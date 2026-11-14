#include "WindowTitle.h"

namespace BECore {

    namespace {

        constexpr StringView kEditorTitleSeparator = " \xE2\x80\x94 ";  // " — "

    }  // namespace

    eastl::string ComposeWindowTitle(StringView configTitle, StringView gameName, bool editorMode) {
        eastl::string title;
        if (editorMode) {
            if (!gameName.empty()) {
                title.append(gameName.data(), gameName.size());
                title.append(kEditorTitleSeparator.data(), kEditorTitleSeparator.size());
            }
            title.append(kEditorWindowTitle.data(), kEditorWindowTitle.size());
            return title;
        }
        const StringView chosen = !configTitle.empty() ? configTitle : (!gameName.empty() ? gameName : kEngineWindowTitle);
        title.assign(chosen.data(), chosen.size());
        return title;
    }

}  // namespace BECore
