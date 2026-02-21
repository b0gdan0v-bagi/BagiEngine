#include "ConfigEditorWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/Format/Format.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Generated/ConfigEditorWidget.gen.hpp>
#include <imgui.h>
#include <pugixml.hpp>

namespace BECore {

    // =========================================================================
    // IWidget interface
    // =========================================================================

    bool ConfigEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void ConfigEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(800.0f, 500.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Config Editor")) {
            ImGui::End();
            return;
        }

        auto& configManager = CoreManager::GetConfigManager();
        const auto configNames = configManager.GetAllConfigNames();

        // ----------------------------------------------------------------
        // Left panel - config list
        // ----------------------------------------------------------------
        const float leftPanelWidth = 220.0f;
        ImGui::BeginChild("##config_list", ImVec2(leftPanelWidth, 0.0f), true);

        ImGui::TextUnformatted("Configs");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", configNames.size());
        ImGui::Separator();

        for (const auto& name : configNames) {
            const bool isSelected = (name == _selectedConfig);
            if (ImGui::Selectable(name.CStr(), isSelected)) {
                if (name != _selectedConfig) {
                    _selectedConfig = name;
                    _hasUnsavedChanges = false;
                    _editBuffers.clear();
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndChild();

        // ----------------------------------------------------------------
        // Right panel - XML tree editor
        // ----------------------------------------------------------------
        ImGui::SameLine();
        ImGui::BeginChild("##config_editor", ImVec2(0.0f, 0.0f), true);

        if (_selectedConfig.Empty()) {
            ImGui::TextDisabled("Select a config on the left to start editing.");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        // Toolbar
        const auto filePath = configManager.GetConfigFilePath(_selectedConfig);
        const auto filePathStr = filePath.string();

        ImGui::TextUnformatted(_selectedConfig.CStr());
        if (!filePathStr.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", filePathStr.c_str());
        }

        ImGui::Separator();

        const bool canSave = _hasUnsavedChanges;
        if (!canSave) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save")) {
            if (configManager.SaveConfig(_selectedConfig)) {
                _hasUnsavedChanges = false;
            }
        }
        if (!canSave) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            if (configManager.ReloadConfig(_selectedConfig)) {
                _hasUnsavedChanges = false;
                _editBuffers.clear();
            }
        }

        if (_hasUnsavedChanges) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(unsaved changes)");
        }

        ImGui::Separator();

        // XML tree
        const XmlNode rootNode = configManager.GetConfig(_selectedConfig);
        if (!rootNode) {
            ImGui::TextDisabled("(empty or failed to load)");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const pugi::xml_node pugiRoot = rootNode.GetPugiNode();
        RenderXmlNode(pugiRoot, eastl::string(rootNode.Name().data(), rootNode.Name().size()));

        ImGui::EndChild();
        ImGui::End();
    }

    void ConfigEditorWidget::Draw() {
        // ImGui rendering is handled by ImGuiWidget::Draw()
    }

    // =========================================================================
    // XML tree rendering
    // =========================================================================

    void ConfigEditorWidget::RenderXmlNode(pugi::xml_node node, const eastl::string& nodePath) {
        if (node.type() != pugi::node_element) {
            return;
        }

        const bool hasChildren = node.first_child() && node.first_child().type() == pugi::node_element;
        const bool hasAttributes = static_cast<bool>(node.first_attribute());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        const bool nodeOpen = ImGui::TreeNodeEx(nodePath.c_str(), flags, "%s", node.name());

        if (nodeOpen) {
            // Render attributes as editable fields in a two-column table
            if (hasAttributes) {
                ImGui::Indent(8.0f);
                if (ImGui::BeginTable("##attrs", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(attr.name());

                        ImGui::TableSetColumnIndex(1);
                        const eastl::string attrKey = Format("{}@{}", nodePath, attr.name());
                        const eastl::string inputLabel = Format("##{}", attrKey);
                        char* buf = GetOrCreateBuffer(attrKey, attr.value());
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText(inputLabel.c_str(), buf, 128, ImGuiInputTextFlags_EnterReturnsTrue)) {
                            attr.set_value(buf);
                            _hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::Unindent(8.0f);
            }

            // Recurse into element children
            size_t childIndex = 0;
            for (auto child = node.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    const eastl::string childPath = Format("{}/{}[{}]", nodePath, child.name(), childIndex);
                    RenderXmlNode(child, childPath);
                    ++childIndex;
                } else if (child.type() == pugi::node_pcdata) {
                    // Text content
                    const eastl::string textKey = Format("{}#text", nodePath);
                    char* buf = GetOrCreateBuffer(textKey, child.value());
                    const eastl::string inputLabel = Format("##{}", textKey);
                    if (ImGui::InputText(inputLabel.c_str(), buf, 128, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        child.set_value(buf);
                        _hasUnsavedChanges = true;
                    }
                }
            }

            ImGui::TreePop();
        }
    }

    char* ConfigEditorWidget::GetOrCreateBuffer(const eastl::string& key, eastl::string_view initialValue) {
        auto it = _editBuffers.find(key);
        if (it == _editBuffers.end()) {
            auto& arr = _editBuffers[key];
            arr.fill('\0');
            const size_t copyLen = std::min(initialValue.size(), arr.size() - 1);
            std::copy_n(initialValue.data(), copyLen, arr.data());
            return arr.data();
        }
        return it->second.data();
    }

}  // namespace BECore
