#include "ConfigEditorPanelModel.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ILogSink.h>
#include <CoreSDL/SDLUtils.h>
#include <Generated/ConfigEditorPanelModel.gen.hpp>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ConfigEditor/FieldInspectors.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>

namespace BECore {

    namespace {

        constexpr StringView kTreeSlotName = "configTree";

    }  // namespace

    void ConfigEditorPanelModel::Initialize() {
        auto& schema = ConfigFieldSchema::GetInstance();

        schema.RegisterFactoryTypes<ILogSink>(ConfigNames::logger, "sink/@type");
        schema.RegisterEnum<LogLevel>(ConfigNames::logger, "sink/@minLevel");
        schema.RegisterFlags("SDLWindowConfig"_intern, "window/@windowFlags", SDLUtils::GetWindowFlagNames());

        Subscribe<ConfigEvents::ConfigChangedEvent, &ConfigEditorPanelModel::OnConfigChanged>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        // The closure keeps `this` only: it is called inside the frame and the handle ends with the model
        _treeSlot = PanelSlots::GetInstance().Register(kTreeSlotName, [this] { DrawTree(); });
    }

    TypedRef ConfigEditorPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    ConfigManager& ConfigEditorPanelModel::Manager() const {
        return _configManager != nullptr ? *_configManager : CoreManager::GetConfigManager();
    }

    void ConfigEditorPanelModel::AppendDocuments(Vector<EditorDocumentState>& out) {
        if (!_selectedConfig.Empty()) {
            out.push_back(MakeConfigDocument(Manager(), EditorDocumentKind::Config, _selectedConfig.ToStringView(), !_staged.IsEmpty(), _selectedConfig));
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
        const bool hadEdits = !_staged.IsEmpty();
        ClearTransientState();  // the inspectors' state may point into the replaced document too
        _showChangedOnDisk = _showChangedOnDisk || hadEdits;
        _viewDirty = true;
        return hadEdits;
    }

    void ConfigEditorPanelModel::SelectConfig(PoolString name) {
        _selectedConfig = name;
        _showChangedOnDisk = false;
        ClearTransientState();
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::ReloadSelectedConfig() {
        _ownWrite = true;
        Manager().ReloadConfig(_selectedConfig);
        _ownWrite = false;
        _showChangedOnDisk = false;
        ClearTransientState();
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::SaveSelectedConfig() {
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
        _viewDirty = true;
    }

    void ConfigEditorPanelModel::ClearTransientState() {
        _staged.Clear();
        ClearFieldInspectorState();
    }

    void ConfigEditorPanelModel::DrawTree() {
        const XmlNode root = Manager().GetConfig(_selectedConfig);
        if (root) {
            _tree.Draw(root, _selectedConfig);
        }
    }

    void ConfigEditorPanelModel::SelectByName(StringView name) {
        // The name comes from a file or a control request: it is matched against the loaded configs and never interned
        for (const PoolString config : Manager().GetAllConfigNames()) {
            if (config.ToStringView() != name) {
                continue;
            }
            if (config != _selectedConfig) {
                if (_staged.IsEmpty()) {
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
        if (_staged.IsEmpty()) {
            ReloadSelectedConfig();
        } else {
            _showReloadConfirm = true;
        }
        _viewDirty = true;
    }

    bool ConfigEditorPanelModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kConfigSave)) {
            // The rule of the Save button: a config is selected and has staged changes
            if (!_selectedConfig.Empty() && !_staged.IsEmpty()) {
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
        // The inspectors stage edits while the tree is drawn, and the document under the selection may be replaced: both are read each frame, cheaply
        ConfigManager& configManager = Manager();
        const size_t configCount = configManager.GetConfigCount();
        const bool configPresent = !_selectedConfig.Empty() && static_cast<bool>(configManager.GetConfig(_selectedConfig));
        if (_viewDirty || configCount != _builtConfigCount || configPresent != _builtConfigPresent || _staged.IsEmpty() == _view.hasStaged) {
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
        _view.hasStaged = !_staged.IsEmpty();
        _view.changedOnDisk = hasSelection && _showChangedOnDisk;
        _view.showTree = hasSelection && _builtConfigPresent;
        _view.configMissing = hasSelection && !_builtConfigPresent;
        _view.reloadConfirmOpen = hasSelection && _showReloadConfirm;
        _view.switchConfirmOpen = hasSelection && _showSwitchConfirm;
        _view.reloadMessage = Format("You have unsaved changes in '{}'.", _selectedConfig.ToStringView());
        _view.switchMessage = _view.reloadMessage;
        _viewDirty = false;
    }

}  // namespace BECore
