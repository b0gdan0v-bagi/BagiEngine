#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Events/SubscriptionHolder.h>
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

    /// What `GeneratePanel.xml` binds to, as `art.<section>.<field>`
    struct ArtGenPanelView {
        BE_CLASS(ArtGenPanelView)

        BE_REFLECT_FIELD ArtGenServiceView service;
        BE_REFLECT_FIELD ArtGenSetupView setup;
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
     * Commands (`EditorCommandIds`, run only while the panel is open): `artgen.service` (`start`: the control API first when it is off, the bridge once
     * it listens; `disconnect`) and `artgen.setup` (`plan`, `install`, `start`, `stop`, `cancel`, `dismiss`).
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

    private:
        [[nodiscard]] ArtGenSession& Session() const;
        [[nodiscard]] ControlServerStatus ControlStatus();
        void OnSelectionChanged(const SceneSelectionChangedEvent& event);
        void RunService(StringView argument);
        void RunSetup(StringView argument);
        void StartBridge(uint16_t port);
        [[nodiscard]] bool IsStarting();
        void RefreshService(const ControlServerStatus& control, bool starting);
        void RefreshSetup();

        ArtGenPanelView _view;
        ArtGenPanelServices _services;

        // The bridge this panel started: when (the services' clock), so the line says "starting" until it polls; why it could not start
        double _bridgeStartedAt = -1.0;
        bool _bridgeWaitsForControl = false;  // Start the service turned the control API on: the bridge starts once it listens
        String _bridgeError;
        String _setupError;

        // The keys of the last refresh: the view is made again only when one moved or the model changed what it shows
        bool _stale = true;
        uint64_t _version = 0;
        ControlServerStatus _control;
        bool _connected = false;
        bool _starting = false;
    };

}  // namespace BECore
