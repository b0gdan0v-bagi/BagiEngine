#include "ConfigEditorWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ILogSink.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/IDeserializer.h>
#include <CoreSDL/SDLUtils.h>
#include <EASTL/iterator.h>
#include <Generated/ConfigEditorWidget.gen.hpp>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ConfigEditor/FieldInspectors.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {
        constexpr float kCancelButtonWidth = 100.0f;
        // Room for a path of a few levels, so a key is built in one block of the scratch arena
        constexpr size_t kPathReserve = 128;

        /// A comment before the first element must not turn a branch into a leaf
        bool HasElementChild(pugi::xml_node node) {
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    bool ConfigEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        auto& schema = ConfigFieldSchema::GetInstance();

        schema.RegisterFactoryTypes<ILogSink>(ConfigNames::logger, "sink/@type");
        schema.RegisterEnum<LogLevel>(ConfigNames::logger, "sink/@minLevel");
        schema.RegisterFlags("SDLWindowConfig"_intern, "window/@windowFlags", SDLUtils::GetWindowFlagNames());

        Subscribe<EditorCommandEvent, &ConfigEditorWidget::OnCommand>(this);
        Subscribe<ConfigEvents::ConfigChangedEvent, &ConfigEditorWidget::OnConfigChanged>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        return true;
    }

    void ConfigEditorWidget::AppendDocuments(Vector<EditorDocumentState>& out) {
        if (!_selectedConfig.Empty()) {
            out.push_back(MakeConfigDocument(Manager(), EditorDocumentKind::Config, _selectedConfig.ToStringView(), !_staged.IsEmpty(), _selectedConfig));
        }
    }

    ConfigManager& ConfigEditorWidget::Manager() {
        return _configManager != nullptr ? *_configManager : CoreManager::GetConfigManager();
    }

    void ConfigEditorWidget::OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (!_ownWrite) {
            DropStagedForChange(event.name);
        }
    }

    bool ConfigEditorWidget::DropStagedForChange(PoolString changedConfig) {
        if (changedConfig != _selectedConfig) {
            return false;
        }
        const bool hadEdits = !_staged.IsEmpty();
        ClearTransientState();  // the inspectors' state may point into the replaced document too
        _showChangedOnDisk = _showChangedOnDisk || hadEdits;
        return hadEdits;
    }

    void ConfigEditorWidget::SelectConfig(PoolString name) {
        _selectedConfig = name;
        _showChangedOnDisk = false;
        ClearTransientState();
    }

    void ConfigEditorWidget::ReloadSelectedConfig() {
        _ownWrite = true;
        Manager().ReloadConfig(_selectedConfig);
        _ownWrite = false;
        _showChangedOnDisk = false;
        ClearTransientState();
    }

    void ConfigEditorWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewConfigEditor) {
            _open = !_open;
        }
        // The same rule as the Save button: a config is selected and has staged changes
        if (event.commandId == EditorCommandIds::kConfigSave && !_selectedConfig.Empty() && !_staged.IsEmpty()) {
            SaveSelectedConfig();
        }
    }

    void ConfigEditorWidget::SaveSelectedConfig() {
        ConfigManager& configManager = Manager();
        const XmlNode rootNode = configManager.GetConfig(_selectedConfig);
        if (rootNode) {
            _staged.ApplyTo(rootNode.GetPugiNode());
        }
        _ownWrite = true;
        const bool saved = configManager.SaveConfig(_selectedConfig);
        _ownWrite = false;
        if (saved) {
            _showChangedOnDisk = false;
            ClearTransientState();
        }
    }

    void ConfigEditorWidget::Update() {
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(900.0f, 550.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Config Editor", &_open)) {
            ImGui::End();
            return;
        }

        ConfigManager& configManager = Manager();
        const auto configNames = configManager.GetAllConfigNames();

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
                        SelectConfig(name);
                    }
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##config_editor", ImVec2(0.0f, 0.0f), true);

        if (_selectedConfig.Empty()) {
            EmptyState(ICON_LC_FILE_TEXT, "Select a config on the left to start editing.");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const auto filePath = configManager.GetConfigFilePath(_selectedConfig);
        const String filePathStr(filePath.View());

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
        if (IconTextButton(ICON_LC_SAVE, "Save", "Write the staged changes to the config file")) {
            SaveSelectedConfig();
        }
        if (!hasStagedChanges) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (IconTextButton(ICON_LC_REFRESH_CW, "Reload", "Read the config again from disk")) {
            if (hasStagedChanges) {
                _showReloadConfirm = true;
            } else {
                ReloadSelectedConfig();
            }
        }

        if (hasStagedChanges) {
            ImGui::SameLine();
            ImGui::TextColored(GetImGuiPalette().warning, "(unsaved changes)");
        }
        if (_showChangedOnDisk) {
            ImGui::SameLine();
            ImGui::TextColored(GetImGuiPalette().warning, "changed on disk, staged edits dropped");
        }

        ImGui::Separator();

        const XmlNode rootNode = configManager.GetConfig(_selectedConfig);
        if (!rootNode) {
            EmptyState(ICON_LC_CIRCLE_ALERT, "This config is empty or failed to load");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const pugi::xml_node pugiRoot = rootNode.GetPugiNode();
        {
            const ScratchScope scratch;
            ArenaString rootPath;
            rootPath.reserve(kPathReserve);
            rootPath.append(rootNode.Name().data(), rootNode.Name().size());
            RenderXmlNode(pugiRoot, rootPath);
        }

        ImGui::EndChild();

        if (_showReloadConfirm) {
            ImGui::OpenPopup("Unsaved Changes##reload");
            _showReloadConfirm = false;
        }
        if (_showSwitchConfirm) {
            ImGui::OpenPopup("Unsaved Changes##switch");
            _showSwitchConfirm = false;
        }

        if (RenderReloadConfirmPopup()) {
            ReloadSelectedConfig();
        }

        if (ImGui::BeginPopupModal("Unsaved Changes##switch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes in '%s'.", _selectedConfig.CStr());
            ImGui::Text("Switch config and discard changes?");
            ImGui::Separator();
            if (IconTextButton(ICON_LC_UNDO_2, "Discard & Switch")) {
                SelectConfig(_pendingSwitchConfig);
                _pendingSwitchConfig = PoolString{};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (IconTextButton(ICON_LC_X, "Cancel", nullptr, kCancelButtonWidth)) {
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

    void ConfigEditorWidget::RenderXmlNode(pugi::xml_node node, const ArenaString& nodePath) {
        if (node.type() != pugi::node_element) {
            return;
        }

        const StringView path(nodePath.data(), nodePath.size());
        const bool hasChildren = HasElementChild(node);
        const bool hasAttributes = static_cast<bool>(node.first_attribute());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        const bool nodeOpen = ImGui::TreeNodeEx(nodePath.c_str(), flags, "%s", node.name());

        if (nodeOpen) {
            auto& schema = ConfigFieldSchema::GetInstance();

            if (hasAttributes) {
                ImGui::Indent(8.0f);
                if (ImGui::BeginTable("##attrs", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
                        ImGui::TableNextRow();

                        const ScratchScope attrScope;
                        ArenaString attrKey;
                        attrKey.reserve(kPathReserve);
                        FormatTo(eastl::back_inserter(attrKey), "{}@{}", path, attr.name());
                        const StringView attrKeyView(attrKey.data(), attrKey.size());
                        const bool isStaged = _staged.Has(attrKeyView);

                        ImGui::TableSetColumnIndex(0);
                        if (isStaged) {
                            ImGui::TextColored(GetImGuiPalette().warning, "%s*", attr.name());
                        } else {
                            ImGui::TextUnformatted(attr.name());
                        }

                        ImGui::TableSetColumnIndex(1);

                        const StringView domValue{attr.value()};
                        const String* stagedPtr = _staged.Get(attrKeyView);
                        const StringView currentValue = stagedPtr ? StringView{*stagedPtr} : domValue;

                        ArenaString schemaPath;
                        schemaPath.reserve(kPathReserve);
                        FormatTo(eastl::back_inserter(schemaPath), "{}/@{}", node.name(), attr.name());
                        const FieldHint& hint = schema.Resolve(_selectedConfig, StringView(schemaPath.data(), schemaPath.size()), currentValue);

                        const auto result = RenderFieldInspector(attrKeyView, currentValue, hint);
                        if (result.changed) {
                            _staged.Set(attrKeyView, attr, result.newValue);
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::Unindent(8.0f);
            }

            size_t childIndex = 0;
            for (auto child = node.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    const ScratchScope childScope;
                    ArenaString childPath;
                    childPath.reserve(kPathReserve);
                    FormatTo(eastl::back_inserter(childPath), "{}/{}[{}]", path, child.name(), childIndex);
                    RenderXmlNode(child, childPath);
                    ++childIndex;
                } else if (child.type() == pugi::node_pcdata) {
                    const ScratchScope textScope;
                    ArenaString textKeyString;
                    textKeyString.reserve(kPathReserve);
                    FormatTo(eastl::back_inserter(textKeyString), "{}#text", path);
                    const StringView textKey(textKeyString.data(), textKeyString.size());
                    const String* stagedPtr = _staged.Get(textKey);
                    const StringView currentValue = stagedPtr ? StringView{*stagedPtr} : StringView{child.value()};

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
        if (ImGui::BeginPopupModal("Unsaved Changes##reload", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("You have unsaved changes in '%s'.", _selectedConfig.CStr());
            ImGui::Text("Reload from disk and discard all changes?");
            ImGui::Separator();
            if (IconTextButton(ICON_LC_UNDO_2, "Discard & Reload")) {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (IconTextButton(ICON_LC_X, "Cancel", nullptr, kCancelButtonWidth)) {
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

}  // namespace BECore
