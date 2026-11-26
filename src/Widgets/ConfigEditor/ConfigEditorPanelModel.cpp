#include "ConfigEditorPanelModel.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <Generated/ConfigEditorPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/Feedback/ToastEvents.h>

namespace BECore {

    namespace {

        constexpr StringView kTreeSlotName = "configTree";

    }  // namespace

    void ConfigEditorPanelModel::Initialize() {
        Subscribe<ConfigEvents::ConfigChangedEvent, &ConfigEditorPanelModel::OnConfigChanged>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        // The closure keeps `this` only: it is called inside the frame and the handle ends with the model
        _treeSlot = PanelSlots::GetInstance().Register(kTreeSlotName, [this] { DrawConfig(); });
    }

    TypedRef ConfigEditorPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    ConfigManager& ConfigEditorPanelModel::Manager() const {
        return _configManager != nullptr ? *_configManager : CoreManager::GetConfigManager();
    }

    bool ConfigEditorPanelModel::HasUnsavedEdits() const {
        return !_staged.IsEmpty() || _unwritten || (_document && _document->IsDirty());
    }

    void ConfigEditorPanelModel::AppendDocuments(Vector<EditorDocumentState>& out) {
        if (!_selectedConfig.Empty()) {
            out.push_back(MakeConfigDocument(Manager(), EditorDocumentKind::Config, _selectedConfig.ToStringView(), HasUnsavedEdits(), _selectedConfig));
        }
    }

    void ConfigEditorPanelModel::OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (!_ownWrite) {
            DropStagedForChange(event.name);
        }
    }

    bool ConfigEditorPanelModel::DropStagedForChange(PoolString changedConfig) {
        if (changedConfig != _selectedConfig) {
            return false;
        }
        const bool hadEdits = HasUnsavedEdits();
        ResetEdits();
        _showChangedOnDisk = _showChangedOnDisk || hadEdits;
        _viewDirty = true;
        return hadEdits;
    }

    void ConfigEditorPanelModel::SelectConfig(PoolString name) {
        _selectedConfig = name;
        _showChangedOnDisk = false;
        ResetEdits();
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::ReloadSelectedConfig() {
        _ownWrite = true;
        Manager().ReloadConfig(_selectedConfig);
        _ownWrite = false;
        _showChangedOnDisk = false;
        ResetEdits();
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::SaveSelectedConfig() {
        ConfigManager& configManager = Manager();
        if (_document) {
            _document->Apply();
        } else if (const XmlNode rootNode = configManager.GetConfig(_selectedConfig)) {
            _staged.ApplyTo(rootNode.GetPugiNode());
        }
        _ownWrite = true;
        const bool saved = configManager.SaveConfig(_selectedConfig);
        _ownWrite = false;
        if (saved) {
            _showChangedOnDisk = false;
            ResetEdits();
        } else {
            // Applying left the document clean, but the file does not hold its edits
            _unwritten = _document.has_value();
            EditorToastEvent::Emit(ToastLevel::Error, "Save failed: {}"_format(_selectedConfig.ToStringView()));
        }
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::ResetEdits() {
        _staged.Clear();
        _tree.Clear();
        _unwritten = false;
        OpenDocument();
    }

    void ConfigEditorPanelModel::OpenDocument() {
        _document.reset();
        _textModeNote.clear();
        if (_selectedConfig.Empty()) {
            return;
        }
        auto opened = ReflectedConfigDocument::Open(_selectedConfig.ToStringView(), Manager().GetConfig(_selectedConfig).GetPugiNode());
        if (opened) {
            _document.emplace(std::move(*opened));
            _objects.Prepare(*_document);
        } else {
            _textModeNote = Format("Edited as text: {}", opened.error());
        }
    }

    void ConfigEditorPanelModel::DrawConfig() {
        if (_document) {
            _objects.Draw(*_document);
        } else if (const XmlNode root = Manager().GetConfig(_selectedConfig)) {
            _tree.Draw(root);
        }
    }

    void ConfigEditorPanelModel::SelectByName(StringView name) {
        // The name comes from a file or a control request: it is matched against the loaded configs and never interned
        for (const PoolString config : Manager().GetAllConfigNames()) {
            if (config.ToStringView() != name) {
                continue;
            }
            if (config != _selectedConfig) {
                if (!HasUnsavedEdits()) {
                    SelectConfig(config);
                } else {
                    _pendingSwitchConfig = config;
                    _showSwitchConfirm = true;
                }
            }
            break;
        }
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::RequestReload() {
        if (_selectedConfig.Empty()) {
            return;
        }
        if (!HasUnsavedEdits()) {
            ReloadSelectedConfig();
        } else {
            _showReloadConfirm = true;
        }
        _viewDirty = true;
    }

    bool ConfigEditorPanelModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kConfigSave)) {
            // The rule of the Save button: a config is selected and has unsaved edits
            if (!_selectedConfig.Empty() && HasUnsavedEdits()) {
                SaveSelectedConfig();
            }
        } else if (is(EditorCommandIds::kConfigEditorSelect)) {
            SelectByName(argument);
        } else if (is(EditorCommandIds::kConfigEditorReload)) {
            RequestReload();
        } else if (is(EditorCommandIds::kConfigEditorReloadDiscard)) {
            _showReloadConfirm = false;
            if (!_selectedConfig.Empty()) {
                ReloadSelectedConfig();
            }
        } else if (is(EditorCommandIds::kConfigEditorReloadCancel)) {
            _showReloadConfirm = false;
        } else if (is(EditorCommandIds::kConfigEditorSwitchDiscard)) {
            _showSwitchConfirm = false;
            if (!_pendingSwitchConfig.Empty()) {
                SelectConfig(_pendingSwitchConfig);
            }
            _pendingSwitchConfig = PoolString{};
        } else if (is(EditorCommandIds::kConfigEditorSwitchCancel)) {
            _showSwitchConfirm = false;
            _pendingSwitchConfig = PoolString{};
        } else {
            return false;
        }
        _viewDirty = true;
        RebuildView();
        return true;
    }

    void ConfigEditorPanelModel::Update() {
        // The drawers edit while the slot is drawn, and the document under the selection may be replaced: both are read each frame, cheaply
        ConfigManager& configManager = Manager();
        const size_t configCount = configManager.GetConfigCount();
        const bool configPresent = !_selectedConfig.Empty() && static_cast<bool>(configManager.GetConfig(_selectedConfig));
        if (_viewDirty || configCount != _builtConfigCount || configPresent != _builtConfigPresent || HasUnsavedEdits() != _view.hasStaged) {
            RebuildView();
        }
    }

    void ConfigEditorPanelModel::RebuildView() {
        ConfigManager& configManager = Manager();
        const Vector<PoolString> names = configManager.GetAllConfigNames();
        _builtConfigCount = configManager.GetConfigCount();
        _builtConfigPresent = !_selectedConfig.Empty() && static_cast<bool>(configManager.GetConfig(_selectedConfig));

        _view.configs.resize(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            _view.configs[i].name = String(names[i].ToStringView());
            _view.configs[i].selected = names[i] == _selectedConfig;
        }
        _view.configsCount = Format("({})", names.size());

        const bool hasSelection = !_selectedConfig.Empty();
        _view.hasSelection = hasSelection;
        _view.noSelection = !hasSelection;
        _view.configName = String(_selectedConfig.ToStringView());
        _view.filePathText.clear();
        if (hasSelection) {
            const Path filePath = configManager.GetConfigFilePath(_selectedConfig);
            if (!filePath.View().empty()) {
                _view.filePathText = Format("({})", filePath.View());
            }
        }
        _view.hasPath = !_view.filePathText.empty();
        _view.hasStaged = HasUnsavedEdits();
        _view.changedOnDisk = hasSelection && _showChangedOnDisk;
        _view.showTree = hasSelection && _builtConfigPresent;
        _view.configMissing = hasSelection && !_builtConfigPresent;
        _view.textMode = _view.showTree && !_document;
        _view.textModeNote = _textModeNote;
        _view.reloadConfirmOpen = hasSelection && _showReloadConfirm;
        _view.switchConfirmOpen = hasSelection && _showSwitchConfirm;
        _view.reloadMessage = Format("You have unsaved changes in '{}'.", _selectedConfig.ToStringView());
        _view.switchMessage = _view.reloadMessage;
        _viewDirty = false;
    }

}  // namespace BECore
