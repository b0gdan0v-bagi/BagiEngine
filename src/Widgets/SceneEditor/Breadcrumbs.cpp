#include "Breadcrumbs.h"

#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <cstdio>
#include <imgui.h>

namespace BECore {

    namespace {
        /// A name of the longest length the editor lets a user type, with the id suffix after it
        constexpr size_t kCrumbLabelSize = HierarchyModel::kMaxNodeNameLength + 24;
    }  // namespace

    SceneNode* DrawBreadcrumbs(const SceneNode& node, Vector<SceneNode*>& ancestors) {
        HierarchyModel::AncestorsOf(node, ancestors);
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x;
        SceneNode* clicked = nullptr;
        for (size_t i = 0; i <= ancestors.size(); ++i) {
            const bool isLink = i < ancestors.size();
            const char* const name = isLink ? ancestors[i]->GetName().CStr() : node.GetName().CStr();
            if (i > 0) {
                const float width = chevronWidth + spacing + ImGui::CalcTextSize(name).x;
                if (ImGui::GetItemRectMax().x + spacing + width <= right) {
                    ImGui::SameLine(0.0f, spacing);
                }
                ImGui::TextDisabled("%s", ICON_LC_CHEVRON_RIGHT);
                ImGui::SameLine(0.0f, spacing);
            }
            if (!isLink) {
                ImGui::TextUnformatted(name);
                continue;
            }
            char label[kCrumbLabelSize];
            // The name is cut before the suffix can be
            snprintf(label, sizeof(label), "%.*s##crumb%zu", static_cast<int>(HierarchyModel::kMaxNodeNameLength), name, i);
            if (ImGui::TextLink(label)) {
                clicked = ancestors[i];
            }
        }
        return clicked;
    }

}  // namespace BECore
