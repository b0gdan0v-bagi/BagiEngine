#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <BECore/Widgets/PanelAssets/PanelAssetHolder.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Events/ConfigEvents.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>

namespace BECore {

    class ConfigManager;

    /**
     * @brief The editor panel of one panel asset (`<widget type="AssetPanelWidget" asset="FileChangesPanel"/>`): reads the file, hosts its models, draws it
     *
     * `asset` is the config name (the file stem under `config/panels`). The widget keeps the last asset that read with no issue and draws it; a file
     * with issues, or one that could not be read again, shows a banner above it (`PanelAssetHolder`), and a first load that failed shows the banner alone.
     * The window is `<title>###<asset id>`, so a saved layout and the control API's lookup by name find it by the asset's id. With `titleBind` the title is
     * the bound text, made again in `Update` only when the text differs from the last one; an unreadable text leaves the asset's `title`.
     *
     * **Models.** Each `<source>` with a `model` is one `IPanelModel` made by `CreatePanelModel` and registered in the widget's own `PanelDataSources` (the
     * global one stands behind it), so two panels may both name a source `files`. A reload keeps the instance of a source whose name and model did not change.
     * While the panel is closed nothing is drawn and no command reaches a model (the open command apart), as with the panels it replaces. The models are
     * told every change of the open flag (`IPanelModel::OnOpenChanged`) and the end of each drawn frame (`EndUpdate`).
     *
     * A change of the asset's config is only noted by the event and applied at the next `Update` or at the end of the command being run: the event can
     * come from inside a model's own command (applying the panel's file), and a reload may drop that very model.
     *
     * **Window kinds** (`PanelAsset::window`). `Docked` is an ordinary window. `Floating` is `ImGuiWindowFlags_NoDocking`. `Dialogs` is always open (the open flag is forced true every
     * `Update`, and the asset's `command`, when it has one, does not toggle it): the host is a one-pixel window that draws nothing and takes no input, with no close button, so the only
     * thing seen of it is the modals of its popups. A banner of a reload that failed is therefore not visible on a `Dialogs` host; the last good asset keeps working.
     *
     * A click is emitted as `EditorCommandEvent` after the window is closed, never inside the walk (`PanelInterpreter`). Editor only.
     */
    class AssetPanelWidget : public IWidget {
        BE_CLASS(AssetPanelWidget)
    public:
        AssetPanelWidget() = default;
        /// Tells the models of an open panel that it closed, so a mode switch leaves none of them with global state on
        ~AssetPanelWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

        /// The config manager the asset is read from; the one of `CoreManager` unless a test sets another. It must outlive the widget
        void SetConfigManager(const ConfigManager& configs);

        /// Names the config to read, as the `asset` attribute does; call before `Initialize`
        void SetAsset(StringView name);

        [[nodiscard]] bool IsOpen() const {
            return _open;
        }

        /// What `Begin` gets: the title and `###<asset id>`. The window's own `Name` keeps the first one in a floating window, so a test reads it here
        [[nodiscard]] const String& GetWindowTitle() const {
            return _windowTitle;
        }

        [[nodiscard]] const PanelAssetHolder& GetHolder() const {
            return _holder;
        }

    private:
        /// A model and the registration of its object; the handle is declared last so it unregisters before the model dies
        struct ModelEntry {
            String name;
            String modelName;
            IntrusivePtr<IPanelModel> model;
            PanelSourceHandle handle;
        };

        void OnCommand(const EditorCommandEvent& event);
        void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event);
        void OnReloadFailed(const ConfigEvents::ConfigReloadFailedEvent& event);

        bool RunModelCommand(StringView id, StringView argument) const;
        void ApplyPendingReload();
        void LoadAsset();
        void OnAssetChanged();
        void SyncModels();
        void NotifyOpenChanged();
        void DrawBanner() const;
        void SyncTitle();
        [[nodiscard]] const ConfigManager& Configs() const;

        BE_REFLECT_FIELD String _asset;
        PanelAssetHolder _holder;
        PanelInterpreter _interpreter;
        PanelDataSources _sources;
        PanelDrawResult _result;
        Vector<ModelEntry> _models;
        const ConfigManager* _configs = nullptr;
        String _windowTitle;
        /// The text the title was last made from (the asset's `title` or the bound one)
        String _titleText;
        String _titleScratch;
        PanelBinding _titleBinding;
        PanelBindingReader _titleReader;
        bool _hasTitleBinding = false;
        uint32_t _preparedVersion = 0;
        bool _open = false;
        bool _openDecided = false;
        /// The open flag the models were last told
        bool _notifiedOpen = false;
        bool _reloadPending = false;
        /// A reload failure told while a reload is pending: shown after that load, which clears the failure
        bool _failurePending = false;
        String _pendingFailure;
    };

}  // namespace BECore
