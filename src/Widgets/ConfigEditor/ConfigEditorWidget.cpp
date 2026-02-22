#include "ConfigEditorWidget.h"

#include "ConfigFieldSchema.h"
#include "FieldInspectors.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogSinkType.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Generated/ConfigEditorWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    // =========================================================================
    // IWidget interface
    // =========================================================================

    bool ConfigEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        auto& schema = ConfigFieldSchema::GetInstance();

        schema.RegisterEnum<LogSinkType>("LoggerConfig"_intern, "sink/@type");
        schema.RegisterEnum<LogLevel>("LoggerConfig"_intern, "sink/@minLevel");
        schema.RegisterFlags("SDLWindowConfig"_intern, "window/@windowFlags",
                             {"SDL_WINDOW_FULLSCREEN", "SDL_WINDOW_OPENGL", "SDL_WINDOW_OCCLUDED",
                              "SDL_WINDOW_HIDDEN", "SDL_WINDOW_BORDERLESS", "SDL_WINDOW_RESIZABLE",
                              "SDL_WINDOW_MINIMIZED", "SDL_WINDOW_MAXIMIZED",
                              "SDL_WINDOW_MOUSE_GRABBED", "SDL_WINDOW_INPUT_FOCUS",
                              "SDL_WINDOW_MOUSE_FOCUS", "SDL_WINDOW_EXTERNAL",
                              "SDL_WINDOW_MODAL", "SDL_WINDOW_HIGH_PIXEL_DENSITY",
                              "SDL_WINDOW_MOUSE_CAPTURE", "SDL_WINDOW_ALWAYS_ON_TOP",
                              "SDL_WINDOW_UTILITY", "SDL_WINDOW_TOOLTIP", "SDL_WINDOW_POPUP_MENU",
                              "SDL_WINDOW_KEYBOARD_GRABBED", "SDL_WINDOW_VULKAN",
                              "SDL_WINDOW_METAL", "SDL_WINDOW_TRANSPARENT",
                              "SDL_WINDOW_NOT_FOCUSABLE"});

        return true;
    }

    void ConfigEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(900.0f, 550.0f), ImGuiCond_FirstUseEver);
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
                    if (!_staged.IsEmpty()) {
                        _pendingSwitchConfig = name;
                        _showSwitchConfirm = true;
                    } else {
                        _selectedConfig = name;
                        ClearTransientState();
                    }
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

        const bool hasStagedChanges = !_staged.IsEmpty();

        if (!hasStagedChanges) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save")) {
            const XmlNode rootNode = configManager.GetConfig(_selectedConfig);
            if (rootNode) {
                _staged.ApplyTo(rootNode.GetPugiNode());
            }
            if (configManager.SaveConfig(_selectedConfig)) {
                ClearTransientState();
            }
        }
        if (!hasStagedChanges) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            if (hasStagedChanges) {
                _showReloadConfirm = true;
            } else {
                configManager.ReloadConfig(_selectedConfig);
                ClearTransientState();
            }
        }

        if (hasStagedChanges) {
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

        // ----------------------------------------------------------------
        // Confirmation popups (rendered outside child windows)
        // ----------------------------------------------------------------
        if (_showReloadConfirm) {
            ImGui::OpenPopup("Unsaved Changes##reload");
            _showReloadConfirm = false;
        }
        if (_showSwitchConfirm) {
            ImGui::OpenPopup("Unsaved Changes##switch");
            _showSwitchConfirm = false;
        }

        if (RenderReloadConfirmPopup()) {
            configManager.ReloadConfig(_selectedConfig);
            ClearTransientState();
        }

        // Switch-config confirmation popup
        if (ImGui::BeginPopupModal("Unsaved Changes##switch", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes in '%s'.", _selectedConfig.CStr());
            ImGui::Text("Switch config and discard changes?");
            ImGui::Separator();
            if (ImGui::Button("Discard & Switch", ImVec2(160.0f, 0.0f))) {
                _selectedConfig = _pendingSwitchConfig;
                _pendingSwitchConfig = PoolString{};
                ClearTransientState();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
                _pendingSwitchConfig = PoolString{};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

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
            auto& schema = ConfigFieldSchema::GetInstance();

            // Render attributes as typed inspector controls in a two-column table
            if (hasAttributes) {
                ImGui::Indent(8.0f);
                if (ImGui::BeginTable("##attrs", 2,
                                      ImGuiTableFlags_SizingFixedFit |
                                          ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
                        ImGui::TableNextRow();

                        // Column 0: attribute name with staged indicator
                        const eastl::string attrKey = Format("{}@{}", nodePath, attr.name());
                        const bool isStaged = _staged.Has(attrKey);

                        ImGui::TableSetColumnIndex(0);
                        if (isStaged) {
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s*", attr.name());
                        } else {
                            ImGui::TextUnformatted(attr.name());
                        }

                        // Column 1: type-appropriate inspector
                        ImGui::TableSetColumnIndex(1);

                        const eastl::string_view domValue{attr.value()};
                        const eastl::string* stagedPtr = _staged.Get(attrKey);
                        const eastl::string_view currentValue =
                            stagedPtr ? eastl::string_view{*stagedPtr} : domValue;

                        const eastl::string schemaPath =
                            Format("{}/@{}", node.name(), attr.name());
                        const FieldHint hint =
                            schema.Resolve(_selectedConfig, schemaPath, currentValue);

                        const auto result =
                            RenderFieldInspector(attrKey, currentValue, hint);
                        if (result.changed) {
                            _staged.Set(attrKey, attr, result.newValue);
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
                    const eastl::string childPath =
                        Format("{}/{}[{}]", nodePath, child.name(), childIndex);
                    RenderXmlNode(child, childPath);
                    ++childIndex;
                } else if (child.type() == pugi::node_pcdata) {
                    const eastl::string textKey = Format("{}#text", nodePath);
                    const eastl::string* stagedPtr = _staged.Get(textKey);
                    const eastl::string_view currentValue =
                        stagedPtr ? eastl::string_view{*stagedPtr}
                                  : eastl::string_view{child.value()};

                    const FieldHint stringHint{FieldType::String};
                    const auto result = RenderFieldInspector(textKey, currentValue, stringHint);
                    if (result.changed) {
                        _staged.SetText(textKey, child, result.newValue);
                    }
                }
            }

            ImGui::TreePop();
        }
    }

    bool ConfigEditorWidget::RenderReloadConfirmPopup() {
        bool confirmed = false;
        if (ImGui::BeginPopupModal("Unsaved Changes##reload", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes in '%s'.", _selectedConfig.CStr());
            ImGui::Text("Reload from disk and discard all changes?");
            ImGui::Separator();
            if (ImGui::Button("Discard & Reload", ImVec2(140.0f, 0.0f))) {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return confirmed;
    }

    void ConfigEditorWidget::ClearTransientState() {
        _staged.Clear();
        ClearFieldInspectorState();
    }

} // namespace BECore
