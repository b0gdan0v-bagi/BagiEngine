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
#include <Widgets/EditorLayout/EditorPanelRegistry.h>
#include <Widgets/Feedback/UIMotion.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PanelAssets/PanelBars.h>
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
     * **Fade.** A `Docked` or `Floating` window fades in when it is opened and out when it is closed; one that is open when the widget first reads its
     * asset is opaque at once. While it fades out the panel counts as closed (no command, no `Update`, the models already told) and the window is
     * still begun, with no input and an empty body. `SetOpen` (a workspace switch) skips the fade.
     *
     * **Window kinds** (`PanelAsset::window`). `Docked` is an ordinary window. `Floating` is `ImGuiWindowFlags_NoDocking`. `Dialogs` is always open (the open flag is forced true every
     * `Update`, and the asset's `command`, when it has one, does not toggle it): the host is a one-pixel window that draws nothing and takes no input, with no close button, so the only
     * thing seen of it is the modals of its popups. A banner of a reload that failed is therefore not visible on a `Dialogs` host; the last good asset keeps working.
     *
     * `Bar` is no window at all: `Update` runs the models and keeps the asset registered in `PanelBarsRegistry` under the config name, and the C++ window that holds the
     * bar calls `PanelBarsRegistry::Draw(<asset name>)`, which is `DrawBar` here. The open flag is forced true and the asset's `command` is ignored, as for `Dialogs`. The
     * models' `EndUpdate` comes at the end of `DrawBar`, or in the next `Update` when nobody drew the bar in between. A bad reload shows as one error line above the bar.
     * The widget must be listed before the window that draws it in `WidgetsConfig.xml`, so the models update before the draw.
     *
     * A click is emitted as `EditorCommandEvent` after the window is closed, never inside the walk (`PanelInterpreter`). Editor only.
     */
    class AssetPanelWidget : public IWidget, public IPanelBar, public IEditorPanel {
        BE_CLASS(AssetPanelWidget)
    public:
        AssetPanelWidget() = default;
        /// Tells the models of an open panel that it closed, so a mode switch leaves none of them with global state on
        ~AssetPanelWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

        /// `IPanelBar`: the bar of a `Bar` asset, drawn into the window the caller has begun
        void DrawBar() override;

        /// The config manager the asset is read from; the one of `CoreManager` unless a test sets another. It must outlive the widget
        void SetConfigManager(const ConfigManager& configs);

        /// Names the config to read, as the `asset` attribute does; call before `Initialize`
        void SetAsset(StringView name);

        [[nodiscard]] bool IsOpen() const override {
            return _open;
        }

        void SetOpen(bool open) override;

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
        void DrawBarBanner() const;
        /// A frame of a closed window that has not faded out yet; nothing once it has
        void DrawFadingOut();
        void EmitClick();
        /// Keeps the registration of a `Bar` asset and drops it when a reload made the asset another kind
        void SyncBarRegistration(bool bar);
        /// Keeps a `Docked` asset, and no other kind, in EditorPanelRegistry under the asset's id
        void SyncPanelRegistration(const PanelAsset& asset);
        /// The `EndUpdate` of a bar frame whose `Update` ran, once
        void EndBarFrame();
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
        Fade _fade;
        /// The size of what the open window drew last, which the empty body of a fading one keeps
        float _contentWidth = 0.0f;
        float _contentHeight = 0.0f;
        uint32_t _preparedVersion = 0;
        bool _open = false;
        bool _openDecided = false;
        /// The open flag the models were last told
        bool _notifiedOpen = false;
        bool _reloadPending = false;
        /// A reload failure told while a reload is pending: shown after that load, which clears the failure
        bool _failurePending = false;
        String _pendingFailure;
        /// An `Update` of a `Bar` asset ran and its `EndUpdate` is still owed
        bool _barFrameOpen = false;
        /// The id the panel is registered under in EditorPanelRegistry
        String _panelId;
        /// Declared last, as the next one: they unregister before the members the bar and the panel use are gone
        PanelBarHandle _barHandle;
        EditorPanelHandle _panelHandle;
    };

}  // namespace BECore
