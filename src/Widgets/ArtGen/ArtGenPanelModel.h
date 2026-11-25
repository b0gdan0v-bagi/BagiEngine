#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ArtGen/ArtGenPanelSlots.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/ArtGen/ArtGenVerdict.h>
#include <Widgets/ArtGen/Reference/ArtGenReferencePanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ControlService;
    struct ArtGenVariantDropEvent;
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
        inline constexpr StringView kUseFile = "useFile";
        inline constexpr StringView kClear = "clear";
        inline constexpr StringView kUseSprite = "useSprite";
        inline constexpr StringView kUseShape = "useShape";
        inline constexpr StringView kGenerate = "generate";
        inline constexpr StringView kImage = "image";
        inline constexpr StringView kSound = "sound";
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
        /// Always `Disabled`: a table column takes its colour from a field of the row, and the plan's state is muted
        BE_REFLECT_FIELD PanelTextStyle stateStyle = PanelTextStyle::Disabled;
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

    /// The reference slot of the session (the Reference section of the Generate tab): the picture the next single Generate starts from
    struct ArtGenRefView {
        BE_CLASS(ArtGenRefView)

        /// The path field of Use file: relative to the game root, or absolute
        BE_REFLECT_FIELD String typedPath;
        /// Use file found no such file; kept until the text changes
        BE_REFLECT_FIELD bool fileMissing = false;
        BE_REFLECT_FIELD bool has = false;
        /// The picture's file name
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String mode;
        BE_REFLECT_FIELD int32_t modeIndex = -1;
        BE_REFLECT_FIELD Vector<ArtGenComboRow> modes;
        BE_REFLECT_FIELD String modeTip;
        /// The mode's default while the slot sets none; a mode change drops the slot's
        BE_REFLECT_FIELD float weight = 0.0f;
    };

    /// The Generate tab's form and the state of its run
    struct ArtGenRunView {
        BE_CLASS(ArtGenRunView)

        BE_REFLECT_FIELD String subject;
        /// 1..`ArtGenSession::kMaxCount`
        BE_REFLECT_FIELD int32_t count = 4;
        BE_REFLECT_FIELD bool randomSeed = true;
        /// Not below 0; sent only without a random seed
        BE_REFLECT_FIELD int64_t seed = 0;
        BE_REFLECT_FIELD bool keepVariants = false;
        /// The preset makes images: the Reference section is shown
        BE_REFLECT_FIELD bool images = true;
        BE_REFLECT_FIELD bool canGenerate = false;
        /// A run of this tab is in work: Cancel is shown
        BE_REFLECT_FIELD bool singleRunInWork = false;
        BE_REFLECT_FIELD bool cancelRequested = false;
        /// A job of a set holds the session: Generate is off until it ends
        BE_REFLECT_FIELD bool setRunning = false;
        BE_REFLECT_FIELD String requestError;
        BE_REFLECT_FIELD bool hasRequestError = false;
        BE_REFLECT_FIELD ArtGenRunState state = ArtGenRunState::Idle;
        /// The run is the last job of a set: its variants are judged on the Set tab
        BE_REFLECT_FIELD bool isSetJob = false;
        BE_REFLECT_FIELD String setJobText;
        /// The images made of the count, the one in work counted by its steps
        BE_REFLECT_FIELD float progress = 0.0f;
        BE_REFLECT_FIELD String progressText;
        BE_REFLECT_FIELD String doneText;
        BE_REFLECT_FIELD String failedText;
        BE_REFLECT_FIELD String cancelledText;
        /// The variants are sounds: the sound list and its Accept instead of the grid
        BE_REFLECT_FIELD bool hasSounds = false;
        BE_REFLECT_FIELD bool hasImages = false;
    };

    /// The Accept section of an image: the picked variant, what Accept would write of it, the sprite name and the answer
    struct ArtGenAcceptView {
        BE_CLASS(ArtGenAcceptView)

        BE_REFLECT_FIELD bool hasPick = false;
        BE_REFLECT_FIELD String seedText;
        /// The drawn box and the written size, or why Accept would refuse the picture
        BE_REFLECT_FIELD String summary;
        BE_REFLECT_FIELD PanelTextStyle summaryStyle = PanelTextStyle::Disabled;
        BE_REFLECT_FIELD bool hasSummary = false;
        /// The sprite (or, for a sound, the clip) name; filled from the subject at the first pick
        BE_REFLECT_FIELD String sprite;
        BE_REFLECT_FIELD bool spriteValid = false;
        /// A name is typed and is not valid
        BE_REFLECT_FIELD bool nameWarning = false;
        BE_REFLECT_FIELD bool haveNode = false;
        /// Cleared while no node is selected
        BE_REFLECT_FIELD bool useOnSelected = false;
        BE_REFLECT_FIELD String useOnSelectedTip;
        BE_REFLECT_FIELD String message;
        BE_REFLECT_FIELD PanelTextStyle messageStyle = PanelTextStyle::Success;
        BE_REFLECT_FIELD bool hasMessage = false;
        /// An image Accept succeeded and the preset makes images: Use as reference and Use as shape take the sprite
        BE_REFLECT_FIELD bool hasAccepted = false;
        /// What Accept of an image writes, with the path the acceptor builds
        BE_REFLECT_FIELD String note;
    };

    /// What Accept does to a sound (`ArtGenSoundSettings`): a change stops the processed preview and drops it, since it is no longer what Accept writes
    struct ArtGenSoundView {
        BE_CLASS(ArtGenSoundView)

        BE_REFLECT_FIELD bool mono = true;
        BE_REFLECT_FIELD bool trim = true;
        /// Kept while `trim` is off; switching it on starts from the default
        BE_REFLECT_FIELD float trimDb = ArtGenSoundSettings::kDefaultTrimDb;
        BE_REFLECT_FIELD float fadeIn = ArtGenSoundSettings::kDefaultFadeInMs;
        BE_REFLECT_FIELD float fadeOut = ArtGenSoundSettings::kDefaultFadeOutMs;
        BE_REFLECT_FIELD bool normalise = true;
        BE_REFLECT_FIELD float peakDb = ArtGenSoundSettings::kDefaultPeakDb;
        /// 0: no limit
        BE_REFLECT_FIELD float maxSeconds = 0.0f;
        /// An index into `rates` (`ArtGenSoundSettings::kRates`)
        BE_REFLECT_FIELD int32_t rateIndex = 0;
        BE_REFLECT_FIELD Vector<String> rates;
    };

    /// The Accept section of a sound; the name field is the image's (`art.accept.sprite`), as are the node check and the message
    struct ArtGenSoundAcceptView {
        BE_CLASS(ArtGenSoundAcceptView)

        /// A sound variant is picked
        BE_REFLECT_FIELD bool hasPick = false;
        /// The picked sound is loaded: Listen as accepted can process it
        BE_REFLECT_FIELD bool canListen = false;
        BE_REFLECT_FIELD bool previewPlaying = false;
        /// The processed preview's length, rate and channels
        BE_REFLECT_FIELD String previewText;
        BE_REFLECT_FIELD bool hasPreview = false;
        BE_REFLECT_FIELD String previewError;
        BE_REFLECT_FIELD bool hasPreviewError = false;
        /// Accept writes over a clip path of the game instead of `assets/artgen/<name>.wav`
        BE_REFLECT_FIELD bool replaceClip = false;
        BE_REFLECT_FIELD String clipPath = "assets/sounds/";
        BE_REFLECT_FIELD bool clipValid = false;
        /// The name or the clip, whichever the target is, is valid
        BE_REFLECT_FIELD bool canAccept = false;
        BE_REFLECT_FIELD String useOnSelectedTip;
        /// What Accept writes: the clip path when replacing one
        BE_REFLECT_FIELD String acceptTip;
    };

    /// What `GeneratePanel.xml` binds to, as `art.<section>.<field>`
    struct ArtGenPanelView {
        BE_CLASS(ArtGenPanelView)

        BE_REFLECT_FIELD ArtGenServiceView service;
        BE_REFLECT_FIELD ArtGenSetupView setup;
        BE_REFLECT_FIELD ArtGenPresetView preset;
        BE_REFLECT_FIELD ArtGenPresetRefView presetRef;
        BE_REFLECT_FIELD ArtGenRefView ref;
        BE_REFLECT_FIELD ArtGenRunView run;
        BE_REFLECT_FIELD ArtGenAcceptView accept;
        BE_REFLECT_FIELD ArtGenSoundView sound;
        BE_REFLECT_FIELD ArtGenSoundAcceptView soundAccept;
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
     * @brief The model of the Generate panel (`config/panels/GeneratePanel.xml`), with no ImGui: the service line, the setup of a local generator, the
     * preset form, the run form, Accept, and the session set up for the control API
     *
     * The model is made with its `AssetPanelWidget` whether the window is open or not, so the control methods, the acceptors and the drop answer exist
     * from the editor's start. Everything else the panel draws is the asset's; what the format cannot say is a slot of `ArtGenPanelSlots`.
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
     * The Generate tab: the reference slot's picture is the slot `artgenReferencePicture` (`ArtGenReferencePanel`), the variants the grid of the slot
     * `artgenVariants`; a click there is noted and applied in `EndUpdate` (a slot notes, the model applies). The thumbnails are loaded from the run's
     * files and evicted when the run replaces them or the model dies. A variant dragged into another panel is answered from `ArtGenVariantDropEvent`,
     * which the model subscribes to in `Initialize`, so also while the panel is closed.
     *
     * Sounds: a run of sound variants is the slot `artgenSounds` (a waveform row per variant with Play / Stop and the dice; clicks noted, applied in
     * `EndUpdate`), its settings and Accept are views. One sound plays at a time on the `Ui` bus: a row, the processed preview or the Set tab's. The Set
     * tab is the slot `artgenSetTab` (`ArtGenSetPanel`); a frame in which it was not drawn (another tab, a collapsed window) and a close of the panel
     * give its pictures and sound back. The model's death stops the sound and drops every picture and clip it loaded.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `artgen.service` (`start`: the control API first when it is off, the bridge once
     * it listens; `disconnect`), `artgen.setup` (`plan`, `install`, `start`, `stop`, `cancel`, `dismiss`), `artgen.preset` (`new`, `duplicate`, `save`,
     * `saveAs`, `revert`, `resetSize`, `acceptDefaults`, `takeSlot`, `removeReference`), `artgen.saveAsOpened` (fills the Save-as name from the form),
     * `artgen.reference` (`useFile`, `clear`, `useSprite`, `useShape`), `artgen.run` (`generate`, `cancel`), `artgen.accept` (`image`, `sound`) and
     * `artgen.listen` (plays the picked sound as Accept would write it, or stops it).
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
        void EndUpdate() override;
        void OnOpenChanged(bool open) override;
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

        /// The variants slot's clicks, applied in `EndUpdate`: a tile picks its variant (and fills an empty name from the subject), the dice puts its
        /// seed into the run form, Use as reference fills the session's slot with the picked picture
        void NotePick(int64_t seed) {
            _pickedSeed = seed;
        }

        void NoteDice(int64_t seed) {
            _diceSeed = seed;
        }

        void NoteUseAsReference(int64_t seed) {
            _referenceSeed = seed;
        }

        /// The sounds slot's Play of the row @p row (an index of the run's variants), or Stop with -1; applied in `EndUpdate`
        void NotePlayRow(int32_t row) {
            _playRow = row;
        }

        /// The row that plays now: an index of the run's variants, `kPreviewRow` or `kSetTabRow`; -1 when nothing does
        [[nodiscard]] int32_t GetPlayingRow() const {
            return _playingRow;
        }

        /// The Set tab's player: one sound plays at a time, so a row's or the preview's stops
        void PlaySetTabSound(const AudioClip& clip) {
            PlaySound(clip, kSetTabRow);
        }

        void StopSound();

        static constexpr int32_t kPreviewRow = -2;
        static constexpr int32_t kSetTabRow = -3;

        /// The variant the Accept section works on
        [[nodiscard]] const Optional<int64_t>& GetSelectedSeed() const {
            return _selectedSeed;
        }

    private:
        friend class ArtGenPanelSlots;

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

        /// The row of @p mode in `kArtGenReferenceModes`; -1 for a mode the table does not know
        [[nodiscard]] static int32_t ModeIndexOf(StringView mode);
        void InitRunRows();
        /// The reference slot is the session's: Use as reference fills it from anywhere, the next Generate sends it
        void RunReference(StringView argument);
        void ApplyRefEdits();
        void RefreshRef();
        void RunRun(StringView argument);
        /// The form of the run every frame (its checks read the typed subject), the run's texts when the session moved
        void UpdateRun(bool sessionMoved);
        void RefreshRunTexts();
        [[nodiscard]] bool PresetMakesImages() const;
        void UpdateAccept();
        void RunAccept(StringView argument);
        /// A panel dropped a dragged variant (ArtGenDrag.h): accepts it as the sprite the event names and answers in the event's result
        void OnVariantDropped(const ArtGenVariantDropEvent& event);
        /// Takes the answer of an image Accept (the button's or a drop's) into the message line and the sprite Use as reference takes
        void NoteImageAccepted(StringView sprite, const std::expected<ArtGenAcceptAnswer, ControlError>& result);
        /// Reads the picked image's drawn box when the pick changes and makes the summary line again when that or a setting changed
        void RefreshAcceptSummary(const ArtGenRun& run, const ArtGenVariant& variant);
        void SyncThumbnails(const ArtGenRun& run);
        void DropThumbnails();
        /// The clicks the variants and sounds slots noted while they drew
        void ApplyNotedClicks();

        void InitSoundRows();
        /// The view's sound settings into the model's, before the view is made again
        void ApplySoundEdits();
        void RefreshSoundView();
        void UpdateSoundAccept();
        void RunListen();
        void AcceptSound();
        void PlaySound(const AudioClip& clip, int32_t row);
        /// Forgets a sound that ended by itself
        void RefreshPlaying();
        /// The Set tab is not on screen: stops its sound and gives its pictures back
        void ReleaseSetTab();
        void SyncSounds(const ArtGenRun& run);
        void DropSounds();
        /// The loaded clip of the picked sound; null when none is picked, it is not a sound or it has not loaded
        [[nodiscard]] const AudioClip* PickedClip() const;

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

        ArtGenReferencePanel _referencePanel;
        Optional<ArtGenReference> _refShown;  // the slot the ref view was made from: an edit applies only over it
        String _typedPathSeen;
        uint64_t _runVersion = 0;
        bool _runStale = true;
        String _requestError;

        // One thumbnail per variant of the run, in its order; a path that leaves the run is evicted
        Vector<ResourceHandle<ITexture>> _thumbnails;
        Vector<String> _thumbnailPaths;
        Vector<ArtGenVerdict> _verdicts;
        Optional<int64_t> _selectedSeed;
        // Noted by the variants slot while it drew, applied in EndUpdate
        Optional<int64_t> _pickedSeed;
        Optional<int64_t> _diceSeed;
        Optional<int64_t> _referenceSeed;

        /// One sound variant: its clip (loaded from the FLAC the service wrote), and once loaded its waveform and numbers
        struct SoundRow {
            String path;
            ResourceHandle<AudioClip> clip;
            Vector<float> waveform;
            ArtGenSoundStats stats;
            bool measured = false;
        };
        Vector<SoundRow> _sounds;
        AudioHandle _playing;
        int32_t _playingRow = -1;
        Optional<int32_t> _playRow;        // noted by the sounds slot
        IntrusivePtr<AudioClip> _preview;  // the picked sound as Accept would write it
        String _previewError;
        ArtGenSoundSettings _soundSettings;
        bool _soundStale = true;
        bool _soundAcceptStale = true;
        bool _tipReplaceClip = false;  // what the Accept tip of a sound was made for
        String _tipClipPath;
        ArtGenSetPanel _setPanel;
        bool _setTabDrawn = false;

        String _acceptMessage;
        bool _acceptFailed = false;
        String _acceptedSprite;  // the sprite the last successful Accept wrote: Use as reference takes it
        bool _acceptStale = true;
        Optional<int64_t> _acceptShownSeed;

        // What Accept would make of the picked image. The file is decoded once per pick (a slider drag must not decode it per frame), the line is made
        // again only when the box or a setting changes
        uint32_t _boundsRun = 0;
        String _boundsPath;
        bool _boundsRead = false;
        Optional<ArtGenIntRect> _drawn;
        int32_t _imageWidth = 0;
        int32_t _imageHeight = 0;
        ArtGenImageSettings _summarySettings;
        bool _summaryValid = false;

        // The keys of the last refresh: the view is made again only when one moved or the model changed what it shows
        bool _stale = true;
        uint64_t _version = 0;
        ControlServerStatus _control;
        bool _connected = false;
        bool _starting = false;
    };

}  // namespace BECore
