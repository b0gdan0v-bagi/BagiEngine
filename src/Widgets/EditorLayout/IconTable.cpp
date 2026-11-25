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
            {"check-check", ICON_LC_CHECK_CHECK},
            {"chevron-down", ICON_LC_CHEVRON_DOWN},
            {"circle-alert", ICON_LC_CIRCLE_ALERT},
            {"circle-plus", ICON_LC_CIRCLE_PLUS},
            {"circle-x", ICON_LC_CIRCLE_X},
            {"cloud", ICON_LC_CLOUD},
            {"copy", ICON_LC_COPY},
            {"crop", ICON_LC_CROP},
            {"download", ICON_LC_DOWNLOAD},
            {"eye", ICON_LC_EYE},
            {"file", ICON_LC_FILE},
            {"file-code", ICON_LC_FILE_CODE},
            {"file-diff", ICON_LC_FILE_DIFF},
            {"file-image", ICON_LC_FILE_IMAGE},
            {"file-plus", ICON_LC_FILE_PLUS},
            {"file-text", ICON_LC_FILE_TEXT},
            {"filter", ICON_LC_FILTER},
            {"focus", ICON_LC_FOCUS},
            {"folder", ICON_LC_FOLDER},
            {"folder-open", ICON_LC_FOLDER_OPEN},
            {"folder-plus", ICON_LC_FOLDER_PLUS},
            {"frame", ICON_LC_FRAME},
            {"gamepad-2", ICON_LC_GAMEPAD_2},
            {"globe", ICON_LC_GLOBE},
            {"grid-2x2-plus", ICON_LC_GRID_2X2_PLUS},
            {"grid-3x2", ICON_LC_GRID_3X2},
            {"grid-3x3", ICON_LC_GRID_3X3},
            {"headphones", ICON_LC_HEADPHONES},
            {"image", ICON_LC_IMAGE},
            {"image-plus", ICON_LC_IMAGE_PLUS},
            {"info", ICON_LC_INFO},
            {"layers", ICON_LC_LAYERS},
            {"layout-dashboard", ICON_LC_LAYOUT_DASHBOARD},
            {"layout-grid", ICON_LC_LAYOUT_GRID},
            {"list", ICON_LC_LIST},
            {"list-checks", ICON_LC_LIST_CHECKS},
            {"loader", ICON_LC_LOADER},
            {"lock", ICON_LC_LOCK},
            {"magnet", ICON_LC_MAGNET},
            {"maximize", ICON_LC_MAXIMIZE},
            {"monitor", ICON_LC_MONITOR},
            {"mouse-pointer-2", ICON_LC_MOUSE_POINTER_2},
            {"mouse-pointer-click", ICON_LC_MOUSE_POINTER_CLICK},
            {"move", ICON_LC_MOVE},
            {"move-3d", ICON_LC_MOVE_3D},
            {"package", ICON_LC_PACKAGE},
            {"package-open", ICON_LC_PACKAGE_OPEN},
            {"package-plus", ICON_LC_PACKAGE_PLUS},
            {"pause", ICON_LC_PAUSE},
            {"pencil", ICON_LC_PENCIL},
            {"play", ICON_LC_PLAY},
            {"plug", ICON_LC_PLUG},
            {"plus", ICON_LC_PLUS},
            {"power", ICON_LC_POWER},
            {"proportions", ICON_LC_PROPORTIONS},
            {"refresh-cw", ICON_LC_REFRESH_CW},
            {"rotate-3d", ICON_LC_ROTATE_3D},
            {"rotate-ccw", ICON_LC_ROTATE_CCW},
            {"rotate-cw", ICON_LC_ROTATE_CW},
            {"ruler", ICON_LC_RULER},
            {"save", ICON_LC_SAVE},
            {"save-all", ICON_LC_SAVE_ALL},
            {"scale-3d", ICON_LC_SCALE_3D},
            {"scan", ICON_LC_SCAN},
            {"scan-search", ICON_LC_SCAN_SEARCH},
            {"search", ICON_LC_SEARCH},
            {"settings", ICON_LC_SETTINGS},
            {"shapes", ICON_LC_SHAPES},
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
            {"unplug", ICON_LC_UNPLUG},
            {"volume-2", ICON_LC_VOLUME_2},
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
        Vector<StringView>& WarnedNames() {
            static Vector<StringView> names;
            return names;
        }

    }  // namespace

    Span<const IconEntry> GetIconTable() {
        return Span<const IconEntry>(kIcons);
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
            LOG_WARNING("Editor: class '{}' names the icon '{}', which is not a row of IconTable (Widgets/EditorLayout/IconTable.cpp)", meta.typeName, meta.icon);
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
