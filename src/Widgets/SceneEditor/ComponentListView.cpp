#include "ComponentListView.h"

#include <imgui.h>

namespace BECore {

    Optional<size_t> DrawComponentList(IComponentListSource& source) {
        Optional<size_t> removed;
        ImGui::BeginDisabled(source.IsReadOnly());
        for (size_t i = 0; i < source.GetCount() && !removed; ++i) {
            ImGui::PushID(static_cast<int>(i));
            bool visible = true;
            if (ImGui::CollapsingHeader(source.GetTypeName(i), source.IsRemovable() ? &visible : nullptr)) {
                ImGui::Indent();
                source.DrawFields(i);
                ImGui::Unindent();
            }
            ImGui::PopID();
            if (!visible) {
                removed = i;
            }
        }
        ImGui::EndDisabled();
        return removed;
    }

}  // namespace BECore
