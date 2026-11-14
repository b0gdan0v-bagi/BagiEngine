#include "IconTable.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/Icons/IconsLucide.h>

namespace BECore {

    namespace {

        constexpr IconEntry kIcons[] = {
            {"arrow-down", ICON_LC_ARROW_DOWN},
            {"arrow-up", ICON_LC_ARROW_UP},
            {"box", ICON_LC_BOX},
            {"bug", ICON_LC_BUG},
            {"check", ICON_LC_CHECK},
            {"chevron-down", ICON_LC_CHEVRON_DOWN},
            {"circle-alert", ICON_LC_CIRCLE_ALERT},
            {"circle-plus", ICON_LC_CIRCLE_PLUS},
            {"circle-x", ICON_LC_CIRCLE_X},
            {"copy", ICON_LC_COPY},
            {"crop", ICON_LC_CROP},
            {"eye", ICON_LC_EYE},
            {"file", ICON_LC_FILE},
            {"file-code", ICON_LC_FILE_CODE},
            {"file-plus", ICON_LC_FILE_PLUS},
            {"file-text", ICON_LC_FILE_TEXT},
            {"folder", ICON_LC_FOLDER},
            {"folder-open", ICON_LC_FOLDER_OPEN},
            {"folder-plus", ICON_LC_FOLDER_PLUS},
            {"frame", ICON_LC_FRAME},
            {"grid-3x3", ICON_LC_GRID_3X3},
            {"image", ICON_LC_IMAGE},
            {"info", ICON_LC_INFO},
            {"layers", ICON_LC_LAYERS},
            {"layout-dashboard", ICON_LC_LAYOUT_DASHBOARD},
            {"list", ICON_LC_LIST},
            {"maximize", ICON_LC_MAXIMIZE},
            {"monitor", ICON_LC_MONITOR},
            {"mouse-pointer-click", ICON_LC_MOUSE_POINTER_CLICK},
            {"move", ICON_LC_MOVE},
            {"pause", ICON_LC_PAUSE},
            {"pencil", ICON_LC_PENCIL},
            {"play", ICON_LC_PLAY},
            {"plus", ICON_LC_PLUS},
            {"proportions", ICON_LC_PROPORTIONS},
            {"refresh-cw", ICON_LC_REFRESH_CW},
            {"rotate-ccw", ICON_LC_ROTATE_CCW},
            {"rotate-cw", ICON_LC_ROTATE_CW},
            {"ruler", ICON_LC_RULER},
            {"save", ICON_LC_SAVE},
            {"scan", ICON_LC_SCAN},
            {"search", ICON_LC_SEARCH},
            {"settings", ICON_LC_SETTINGS},
            {"shield-check", ICON_LC_SHIELD_CHECK},
            {"sliders-horizontal", ICON_LC_SLIDERS_HORIZONTAL},
            {"smartphone", ICON_LC_SMARTPHONE},
            {"sparkles", ICON_LC_SPARKLES},
            {"square", ICON_LC_SQUARE},
            {"step-forward", ICON_LC_STEP_FORWARD},
            {"tablet", ICON_LC_TABLET},
            {"trash-2", ICON_LC_TRASH_2},
            {"triangle-alert", ICON_LC_TRIANGLE_ALERT},
            {"type", ICON_LC_TYPE},
            {"undo-2", ICON_LC_UNDO_2},
            {"x", ICON_LC_X},
        };

        constexpr bool IsSortedAndUnique() {
            for (size_t i = 1; i < std::size(kIcons); ++i) {
                if (!(kIcons[i - 1].name < kIcons[i].name)) {
                    return false;
                }
            }
            return true;
        }
        static_assert(IsSortedAndUnique(), "IconTable.cpp: the rows must be sorted by name, with no duplicates");

        /// Unknown names already reported. Views of the literals in the generated ClassMeta, so no string is copied;
        /// only a name that is not in the table is ever searched here.
        eastl::vector<StringView>& WarnedNames() {
            static eastl::vector<StringView> names;
            return names;
        }

    }  // namespace

    eastl::span<const IconEntry> GetIconTable() {
        return eastl::span<const IconEntry>(kIcons);
    }

    const char* FindIcon(StringView name) {
        size_t low = 0;
        size_t high = std::size(kIcons);
        while (low < high) {
            const size_t mid = low + (high - low) / 2;
            if (kIcons[mid].name < name) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return low < std::size(kIcons) && kIcons[low].name == name ? kIcons[low].glyph : nullptr;
    }

    const char* GetNodeIcon() {
        return ICON_LC_BOX;
    }

    const char* IconOfClass(const ClassMeta& meta) {
        if (meta.icon.empty()) {
            return GetNodeIcon();
        }
        if (const char* glyph = FindIcon(meta.icon)) {
            return glyph;
        }
        auto& warned = WarnedNames();
        if (eastl::find(warned.begin(), warned.end(), meta.icon) == warned.end()) {
            warned.push_back(meta.icon);
            LOG_WARNING(Format("Editor: class '{}' names the icon '{}', which is not a row of IconTable (Widgets/EditorLayout/IconTable.cpp)", meta.typeName, meta.icon).c_str());
        }
        return GetNodeIcon();
    }

    const char* IconOfNode(const SceneNode& node) {
        for (const auto& component : node.GetComponents()) {
            const ClassMeta& meta = component->GetTypeMeta();
            if (!meta.icon.empty()) {
                return IconOfClass(meta);
            }
        }
        return GetNodeIcon();
    }

}  // namespace BECore
