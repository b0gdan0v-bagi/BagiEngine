#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ArtGen/ArtGenPanelSlots.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    class ArtGenSession;
    class ControlService;
    struct SceneSelectionChangedEvent;

    /// The literal `argument`s of the `artgen.*` buttons (EditorCommands.h); any other text does nothing
    namespace ArtGenPanelArgs {
        inline constexpr StringView kStart = "start";
        inline constexpr StringView kDisconnect = "disconnect";
        inline constexpr StringView kPlan = "plan";
        inline constexpr StringView kInstall = "install";
        inline constexpr StringView kStop = "stop";
        inline constexpr StringView kCancel = "cancel";
        inline constexpr StringView kDismiss = "dismiss";
        inline constexpr StringView kNew = "new";
        inline constexpr StringView kDuplicate = "duplicate";
        inline constexpr StringView kSave = "save";
        inline constexpr StringView kSaveAs = "saveAs";
        inline constexpr StringView kRevert = "revert";
        inline constexpr StringView kResetSize = "resetSize";
        inline constexpr StringView kAcceptDefaults = "acceptDefaults";
        inline constexpr StringView kTakeSlot = "takeSlot";
        inline constexpr StringView kRemoveReference = "removeReference";
    }  // namespace ArtGenPanelArgs

    /// Which service line the panel shows: the control API is off, the bridge was started and has not polled yet, nothing polls, or a service polls
    /// with its generator ready or not
    CORE_ENUM(ArtGenServiceLine, uint8_t, ControlOff, Starting, NotConnected, Ready, NotReady)

    /// The line above everything: whether the service talks to the editor and what its generator does
    struct ArtGenServiceView {
        BE_CLASS(ArtGenServiceView)

        BE_REFLECT_FIELD ArtGenServiceLine line = ArtGenServiceLine::ControlOff;
        BE_REFLECT_FIELD String statusText;
        BE_REFLECT_FIELD PanelTextStyle statusStyle = PanelTextStyle::Warning;
        BE_REFLECT_FIELD bool controlListening = false;
        /// Start the service turned the control API on and it is still off
        BE_REFLECT_FIELD bool controlFailed = false;
        BE_REFLECT_FIELD bool connected = false;
        BE_REFLECT_FIELD bool ready = false;
        /// The Start button: the control API is off or no service polls
        BE_REFLECT_FIELD bool canStart = false;
        BE_REFLECT_FIELD String startTip;
        /// How to run the bridge by hand, with the control API's port; shown while no service polls
        BE_REFLECT_FIELD String byHandText;
        BE_REFLECT_FIELD String bridgeError;
        BE_REFLECT_FIELD bool hasBridgeError = false;
        /// Why the generator is not ready, as the service said
        BE_REFLECT_FIELD String reason;
        BE_REFLECT_FIELD bool hasReason = false;
        /// The generator was started by a setup action, so Stop can end it
        BE_REFLECT_FIELD bool startedHere = false;
        BE_REFLECT_FIELD bool setupBusy = false;
        /// A run or a set is in work
        BE_REFLECT_FIELD bool runBusy = false;
    };

    /// One step of the install plan: the tooltip names its licence and where it goes
    struct ArtGenPlanRow {
        BE_CLASS(ArtGenPlanRow)

        BE_REFLECT_FIELD String what;
        BE_REFLECT_FIELD String state;
        BE_REFLECT_FIELD String size;
        BE_REFLECT_FIELD String tooltip;
    };

    /// The setup of a local generator: a plan, an install with its progress, a start or a stop
    struct ArtGenSetupView {
        BE_CLASS(ArtGenSetupView)

        BE_REFLECT_FIELD bool busy = false;
        /// What the busy action does ("Installing the generator...")
        BE_REFLECT_FIELD String doing;
        BE_REFLECT_FIELD String current;
        BE_REFLECT_FIELD bool hasCurrent = false;
        BE_REFLECT_FIELD bool hasProgress = false;
        BE_REFLECT_FIELD float progress = 0.0f;
        BE_REFLECT_FIELD String progressText;
        BE_REFLECT_FIELD String log;
        BE_REFLECT_FIELD bool hasLog = false;
        BE_REFLECT_FIELD bool cancelShown = false;
        BE_REFLECT_FIELD bool cancelRequested = false;
        BE_REFLECT_FIELD bool failed = false;
        BE_REFLECT_FIELD String error;
        /// What a finished start or stop said (a generator started by hand is left alone)
        BE_REFLECT_FIELD String doneLog;
        BE_REFLECT_FIELD bool hasDoneLog = false;
        /// A setup request the session refused
        BE_REFLECT_FIELD String setupError;
        BE_REFLECT_FIELD bool hasSetupError = false;
        /// Set up... (the generator is not installed and no plan is shown) and Start (it is installed and not running)
        BE_REFLECT_FIELD bool notInstalled = false;
        BE_REFLECT_FIELD bool notRunning = false;
        /// Set up and Start are off while a run is in work
        BE_REFLECT_FIELD bool canAct = false;
        BE_REFLECT_FIELD bool planShown = false;
        BE_REFLECT_FIELD Vector<ArtGenPlanRow> plan;
        BE_REFLECT_FIELD String platformText;
        BE_REFLECT_FIELD String rootText;
        /// A step installs from source, so PyPI adds to the download
        BE_REFLECT_FIELD bool fromSource = false;
        BE_REFLECT_FIELD String downloadSize;
        BE_REFLECT_FIELD String downloadTip;
    };

    /// A row of a combo over one of the service's lists or a fixed table: its text, whether it can be picked, and why not
    struct ArtGenComboRow {
        BE_CLASS(ArtGenComboRow)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD bool enabled = true;
        BE_REFLECT_FIELD String tooltip;
    };

    /// What Accept does to an image, as the form says: drawn in the preset's Settings and again in the Accept section of the Generate tab
    struct ArtGenImageSettingsView {
        BE_CLASS(ArtGenImageSettingsView)

        BE_REFLECT_FIELD bool trim = true;
        BE_REFLECT_FIELD int32_t padding = ArtGenImageSettings::kDefaultPadding;
        /// An index into `maxSizes` (`ArtGenImageSettings::kMaxSizeOptions`); -1 for a value a file set that is not a choice
        BE_REFLECT_FIELD int32_t maxSizeIndex = -1;
        BE_REFLECT_FIELD Vector<String> maxSizes;
        BE_REFLECT_FIELD String maxSizeText;
        /// The form sets its own: Defaults is shown
        BE_REFLECT_FIELD bool overridden = false;
    };

    /// The preset section above the tabs: the bar (combo, New, Duplicate, Save, Save as, Revert), its state and the form's fields
    struct ArtGenPresetView {
        BE_CLASS(ArtGenPresetView)

        /// The game's presets, then the engine's starters: a broken one is disabled with its error
        BE_REFLECT_FIELD Vector<ArtGenComboRow> rows;
        BE_REFLECT_FIELD int32_t index = -1;
        /// `name *` while unsaved, `(no preset)` before the service said what there is
        BE_REFLECT_FIELD String preview;
        BE_REFLECT_FIELD bool hasForm = false;
        BE_REFLECT_FIELD bool canNew = false;
        BE_REFLECT_FIELD bool dirty = false;
        BE_REFLECT_FIELD bool canRevert = false;
        /// Saved in the game and unchanged: Save is off
        BE_REFLECT_FIELD bool savedHere = false;
        /// An engine starter, unchanged: Save makes it the game's own
        BE_REFLECT_FIELD bool engineStarter = false;
        /// `unsaved changes` or `not saved yet`, shown while dirty
        BE_REFLECT_FIELD String stateText;
        BE_REFLECT_FIELD String message;
        BE_REFLECT_FIELD PanelTextStyle messageStyle = PanelTextStyle::Success;
        BE_REFLECT_FIELD bool hasMessage = false;
        /// The service's verdict on the saved file the form came from
        BE_REFLECT_FIELD String baseError;
        BE_REFLECT_FIELD bool hasBaseError = false;
        /// The service has not listed presets or workflows yet
        BE_REFLECT_FIELD bool noList = false;

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String prompt;
        BE_REFLECT_FIELD String negative;

        /// The service's workflows without the variants a map of their base reaches, unless the form names one
        BE_REFLECT_FIELD Vector<ArtGenComboRow> workflows;
        BE_REFLECT_FIELD int32_t workflowIndex = -1;
        BE_REFLECT_FIELD String workflowPreview;
        /// The form names a workflow the service does not describe: no Settings
        BE_REFLECT_FIELD bool unknownWorkflow = false;
        BE_REFLECT_FIELD String unknownWorkflowText;
        BE_REFLECT_FIELD bool knownWorkflow = false;
        BE_REFLECT_FIELD bool isAudio = false;
        BE_REFLECT_FIELD bool hasSize = false;
        /// The workflow's defaults while the form sets none; a write is clamped to 1..`ArtGenPresetForm::kMaxImageSide`
        BE_REFLECT_FIELD int32_t width = 0;
        BE_REFLECT_FIELD int32_t height = 0;
        BE_REFLECT_FIELD bool sizeOverridden = false;
        BE_REFLECT_FIELD ArtGenImageSettingsView accept;

        /// The name field of the Save-as dropdown, filled from the form when it opens
        BE_REFLECT_FIELD String saveAsName;
        BE_REFLECT_FIELD bool saveAsValid = false;
    };

    /// The reference the preset file carries (its Settings' Reference section)
    struct ArtGenPresetRefView {
        BE_CLASS(ArtGenPresetRefView)

        BE_REFLECT_FIELD bool has = false;
        /// The file has a `reference` the form cannot read: kept as it is until replaced or removed
        BE_REFLECT_FIELD bool unreadable = false;
        /// The mode's name, for the conditions of the two weight sliders
        BE_REFLECT_FIELD String mode;
        BE_REFLECT_FIELD int32_t modeIndex = -1;
        BE_REFLECT_FIELD Vector<ArtGenComboRow> modes;
        BE_REFLECT_FIELD String modeTip;
        BE_REFLECT_FIELD String image;
        /// The image's file name
        BE_REFLECT_FIELD String imageTip;
        /// The mode's default while the form sets none; a mode change drops the form's
        BE_REFLECT_FIELD float weight = 0.0f;
        BE_REFLECT_FIELD bool canTake = false;
        BE_REFLECT_FIELD bool canRemove = false;
    };

    /// What `GeneratePanel.xml` binds to, as `art.<section>.<field>`
    struct ArtGenPanelView {
        BE_CLASS(ArtGenPanelView)

        BE_REFLECT_FIELD ArtGenServiceView service;
        BE_REFLECT_FIELD ArtGenSetupView setup;
        BE_REFLECT_FIELD ArtGenPresetView preset;
        BE_REFLECT_FIELD ArtGenPresetRefView presetRef;
    };

    /// What the model talks to. The editor keeps every default; a test replaces them before `Initialize`
    struct ArtGenPanelServices {
        ArtGenSession* session = nullptr;                       ///< null: ArtGenSession::GetInstance()
        ControlService* control = nullptr;                      ///< null: CoreManager's; the session registers its methods on it
        MoveOnlyFunction<ControlServerStatus()> controlStatus;  ///< empty: the control service's status
        MoveOnlyFunction<double()> clock;                       ///< monotonic seconds; empty: the steady clock
        /// Starts the bridge for the control API on @p port; false with @p error. Empty: the engine checkout's bridge (ArtGenLaunch.h)
        MoveOnlyFunction<bool(uint16_t port, String& error)> launchBridge;
        /// Asks the application to turn the control API on. Empty: ApplicationEvents::StartControlServerRequestEvent
        MoveOnlyFunction<void()> startControl;
    };

    /**
     * @brief The model of the Generate panel asset, with no ImGui: the service line, the setup of a local generator, and the session set up for the
     * control API
     *
     * The state of a run lives in `ArtGenSession`, which outlives the panel; the model makes its view of it again only when a key moved (the session's
     * `Version()`, whether a service polls, the control API's status, the bridge it started, the end of the bridge's start window), so a steady
     * `Update` allocates nothing. `Initialize` installs the editor's acceptors in the session and registers its control methods (once per service).
     *
     * The preset being edited is the model's `ArtGenPresetForm`: `Update` first applies what the controls wrote into the view (a field that differs from
     * the form, an index that differs from the one shown; a pick loads that preset and drops the edits), then loads the first preset that loads, or a new
     * one on the first workflow, while the form is empty, then makes the view again when the form's revision or the session moved. The workflow's inputs
     * are the slot `artgenWorkflowInputs` (`ArtGenPanelSlots`), which edits the form in place.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `artgen.service` (`start`: the control API first when it is off, the bridge once
     * it listens; `disconnect`), `artgen.setup` (`plan`, `install`, `start`, `stop`, `cancel`, `dismiss`), `artgen.preset` (`new`, `duplicate`, `save`,
     * `saveAs`, `revert`, `resetSize`, `acceptDefaults`, `takeSlot`, `removeReference`) and `artgen.saveAsOpened` (fills the Save-as name from the form).
     */
    class ArtGenPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(ArtGenPanelModel)
    public:
        /// Seconds a started bridge gets to make its first poll before the line says it did not come
        static constexpr double kBridgeStartSeconds = 15.0;

        ArtGenPanelModel();
        ~ArtGenPanelModel() override;

        /// Replaces what the model talks to; before `Initialize` (a test)
        void SetServices(ArtGenPanelServices services);

        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const ArtGenPanelView& GetView() const {
            return _view;
        }

        [[nodiscard]] ArtGenPanelView& GetMutableView() {
            return _view;
        }

        [[nodiscard]] ArtGenSession& GetSession() const;

        /// The preset being edited. A slot may edit it in place: the view follows in the next `Update`
        [[nodiscard]] ArtGenPresetForm& GetForm() {
            return _form;
        }

    private:
        [[nodiscard]] ControlServerStatus ControlStatus();
        void OnSelectionChanged(const SceneSelectionChangedEvent& event);
        void RunService(StringView argument);
        void RunSetup(StringView argument);
        void StartBridge(uint16_t port);
        [[nodiscard]] bool IsStarting();
        void RefreshService(const ControlServerStatus& control, bool starting);
        void RefreshSetup();

        void InitPresetRows();
        void RunPreset(StringView argument);
        /// The Save-as dropdown opened: its name starts as the form's
        void OpenSaveAs();
        void SaveForm(StringView name);
        void SetFormMessage(String message, bool isError);
        /// The view's edits into the form, before the view is made again
        void ApplyPresetEdits();
        void ApplyPresetRefEdits();
        /// The first preset that loads, else a new one on the first workflow, once the service has said what there is
        void LoadFirstPreset();
        void RefreshPreset();
        void RefreshPresetRef();
        void RefreshWorkflowRows(const ArtGenWorkflow* current);

        ArtGenPanelView _view;
        ArtGenPanelServices _services;

        // The bridge this panel started: when (the services' clock), so the line says "starting" until it polls; why it could not start
        double _bridgeStartedAt = -1.0;
        bool _bridgeWaitsForControl = false;  // Start the service turned the control API on: the bridge starts once it listens
        String _bridgeError;
        String _setupError;

        // The preset being edited: loaded from the service's list, a new one, or a copy; Generate sends it inline while it is unsaved
        ArtGenPresetForm _form;
        String _formMessage;
        bool _formMessageIsError = false;
        ArtGenPanelSlots _slots{*this};
        // What the preset view showed of a value the form does not hold as shown: an edit is a view value that differs from these
        int32_t _shownIndex = -1;
        int32_t _shownWorkflowIndex = -1;
        int32_t _shownWidth = 0;
        int32_t _shownHeight = 0;
        int32_t _shownMaxSizeIndex = -1;
        int32_t _shownRefModeIndex = -1;
        float _shownRefWeight = 0.0f;
        Vector<int32_t> _workflowOfRow;  // the session's workflow index of each row of `workflows`
        uint64_t _presetVersion = 0;
        uint64_t _formRevision = 0;
        bool _presetStale = true;

        // The keys of the last refresh: the view is made again only when one moved or the model changed what it shows
        bool _stale = true;
        uint64_t _version = 0;
        ControlServerStatus _control;
        bool _connected = false;
        bool _starting = false;
    };

}  // namespace BECore
