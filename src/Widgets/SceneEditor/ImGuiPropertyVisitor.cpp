#include "ImGuiPropertyVisitor.h"

#include <imgui.h>

namespace BECore {

    namespace {

        void MakeLabel(char* buf, size_t bufSize, eastl::string_view name, int id) {
            snprintf(buf, bufSize, "%.*s##%d", static_cast<int>(name.size()), name.data(), id);
        }

    }  // namespace

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, bool& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::Checkbox(label, &value);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, int8_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_S8, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, uint8_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_U8, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, int16_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_S16, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, uint16_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_U16, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, int32_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragInt(label, &value);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, uint32_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_U32, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, int64_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_S64, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, uint64_t& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_U64, &value, 1.0f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, float& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragFloat(label, &value, 0.1f);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, double& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::DragScalar(label, ImGuiDataType_Double, &value, 0.1);
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, PoolString& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);

        char buf[256] = {};
        const eastl::string_view sv = value.ToStringView();
        size_t copyLen = sv.size() < sizeof(buf) - 1 ? sv.size() : sizeof(buf) - 1;
        memcpy(buf, sv.data(), copyLen);
        buf[copyLen] = '\0';

        if (ImGui::InputText(label, buf, sizeof(buf))) {
            value = PoolString::Intern(eastl::string_view(buf));
            return true;
        }
        return false;
    }

    bool ImGuiPropertyVisitor::Visit(eastl::string_view name, eastl::string& value) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);

        char buf[256] = {};
        size_t copyLen = value.size() < sizeof(buf) - 1 ? value.size() : sizeof(buf) - 1;
        memcpy(buf, value.data(), copyLen);
        buf[copyLen] = '\0';

        if (ImGui::InputText(label, buf, sizeof(buf))) {
            value.assign(buf);
            return true;
        }
        return false;
    }

    bool ImGuiPropertyVisitor::BeginCompound(eastl::string_view name) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        return ImGui::TreeNode(label);
    }

    bool ImGuiPropertyVisitor::VisitEnum(eastl::string_view name,
                                         const eastl::string_view* enumNames,
                                         size_t enumCount,
                                         size_t& selectedIndex) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        const char* preview = selectedIndex < enumCount ? enumNames[selectedIndex].data() : "";
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            for (size_t i = 0; i < enumCount; ++i) {
                bool isSelected = (i == selectedIndex);
                if (ImGui::Selectable(enumNames[i].data(), isSelected)) {
                    selectedIndex = i;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void ImGuiPropertyVisitor::EndCompound() {
        ImGui::TreePop();
    }

}  // namespace BECore
