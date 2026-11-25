#include "ArtGenPanelModel.h"

#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Generated/ArtGenPanelModel.gen.hpp>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenAtlasAccept.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/ArtGen/ArtGenLaunch.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <chrono>

namespace BECore {

    namespace {

        constexpr double kBytesPerGigabyte = 1e9;
        constexpr double kBytesPerMegabyte = 1e6;
        constexpr StringView kNotInstalled = "not_installed";
        constexpr StringView kNotRunning = "not_running";
        constexpr StringView kInstallStep = "install";
        constexpr StringView kBridgeLog = "artgen-bridge.log";
        constexpr StringView kNotConnectedText = "The generation service is not connected";
        constexpr StringView kStartingText = "Starting the generation service...";
        constexpr StringView kStartControlTip = "Turn on the editor's control API and run the generation service for it; it stops when the editor closes";
        constexpr StringView kStartBridgeTip = "Run tools/artgen/artgen_bridge.py of the engine for this editor; it stops when the editor closes";

        double SteadySeconds() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        bool LaunchEngineBridge(uint16_t port, String& error) {
            const Vector<Path> layers = CoreManager::GetFileSystem().GetMountLayers(MountPoints::config);
            const auto script = FindArtGenBridge(layers);
            if (!script) {
                error = Format("no {} under the content roots (a packaged build?)", kArtGenBridgeScript);
                return false;
            }
            return LaunchArtGenBridge(*script, port, EditorUserDirectory() / kBridgeLog, error);
        }

        StringView DoingOf(ArtGenSetupAction action) {
            switch (action) {
                case ArtGenSetupAction::Plan:
                    return "Looking at this machine...";
                case ArtGenSetupAction::Install:
                    return "Installing the generator...";
                case ArtGenSetupAction::Start:
                    return "Starting the generator (a minute the first time)...";
                default:
                    return "Stopping the generator...";
            }
        }

        void SetText(String& field, StringView value) {
            field.assign(value.data(), value.size());
        }

    }  // namespace

    ArtGenPanelModel::ArtGenPanelModel() {
        InitPresetRows();
        InitRunRows();
        InitSoundRows();
    }

    ArtGenPanelModel::~ArtGenPanelModel() {
        StopSound();
        DropThumbnails();
        DropSounds();
        _referencePanel.DropThumbnail();
        _setPanel.DropThumbnails();
    }

    void ArtGenPanelModel::SetServices(ArtGenPanelServices services) {
        _services = std::move(services);
        _stale = true;
    }

    ArtGenSession& ArtGenPanelModel::GetSession() const {
        return _services.session != nullptr ? *_services.session : ArtGenSession::GetInstance();
    }

    ControlServerStatus ArtGenPanelModel::ControlStatus() {
        if (_services.controlStatus) {
            return _services.controlStatus();
        }
        ControlService& control = _services.control != nullptr ? *_services.control : CoreManager::GetControlService();
        return control.GetServerStatus();
    }

    void ArtGenPanelModel::Initialize() {
        Subscribe<SceneSelectionChangedEvent, &ArtGenPanelModel::OnSelectionChanged>(this);
        Subscribe<ArtGenVariantDropEvent, &ArtGenPanelModel::OnVariantDropped>(this);
        ArtGenSession& session = GetSession();
        session.SetGameRoot(CoreManager::GetFileSystem().GetRootPath().View());
        session.SetAcceptor(&AcceptForControl);
        session.SetSoundAcceptor(&AcceptSoundForControl);
        session.SetKeeper(&KeepForControl);
        session.SetAtlasPacker(&PackAtlasForControl);
        session.SetFrameMeasurer(&MeasureFrameForControl);
        session.SetAcceptBatch(&BeginAcceptBatch, &EndAcceptBatch);
        session.RegisterControl(_services.control != nullptr ? *_services.control : CoreManager::GetControlService());
        _slots.Register();
    }

    TypedRef ArtGenPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ArtGenPanelModel::OnSelectionChanged(const SceneSelectionChangedEvent& event) {
        // The pointers are valid for this event only: the session holds the node and checks it alive before use
        GetSession().SetSelectedNode(event.nodes.empty() ? IntrusivePtr<SceneNode>() : IntrusivePtr<SceneNode>(event.nodes.back()));
    }

    void ArtGenPanelModel::Update() {
        ArtGenSession& session = GetSession();
        ApplyPresetEdits();
        LoadFirstPreset();
        if (_presetStale || _presetVersion != session.Version() || _formRevision != _form.GetRevision()) {
            _presetStale = false;
            _presetVersion = session.Version();
            _formRevision = _form.GetRevision();
            RefreshPreset();
        }
        _view.preset.saveAsValid = IsValidSpriteName(StringView(_view.preset.saveAsName));
        ApplyRefEdits();
        const bool sessionMoved = _runVersion != session.Version();
        _runVersion = session.Version();
        UpdateRun(sessionMoved);
        UpdateAccept();
        RefreshPlaying();
        ApplySoundEdits();
        if (_soundStale) {
            _soundStale = false;
            RefreshSoundView();
        }
        if (_view.run.hasSounds && !_view.run.isSetJob) {
            SyncSounds(session.GetRun());
        } else {
            DropSounds();
        }
        UpdateSoundAccept();

        const ControlServerStatus control = ControlStatus();
        if (control.listening && _bridgeWaitsForControl) {
            _bridgeWaitsForControl = false;
            StartBridge(control.port);
        }
        const bool connected = session.IsServiceConnected();
        if (connected && _bridgeStartedAt >= 0.0) {
            _bridgeStartedAt = -1.0;
            _stale = true;
        }
        const bool starting = !connected && IsStarting();
        if (!_stale && _version == session.Version() && _control == control && _connected == connected && _starting == starting) {
            return;
        }
        _stale = false;
        _version = session.Version();
        _control = control;
        _connected = connected;
        _starting = starting;
        RefreshService(control, starting);
        RefreshSetup();
    }

    void ArtGenPanelModel::EndUpdate() {
        ApplyNotedClicks();
        if (!std::exchange(_setTabDrawn, false)) {
            ReleaseSetTab();
        }
    }

    void ArtGenPanelModel::OnOpenChanged(bool open) {
        if (!open) {
            ReleaseSetTab();
        }
    }

    bool ArtGenPanelModel::IsStarting() {
        if (_bridgeStartedAt < 0.0) {
            return false;
        }
        const double now = _services.clock ? _services.clock() : SteadySeconds();
        return now - _bridgeStartedAt < kBridgeStartSeconds;
    }

    void ArtGenPanelModel::StartBridge(uint16_t port) {
        _bridgeError.clear();
        _stale = true;
        const bool launched = _services.launchBridge ? _services.launchBridge(port, _bridgeError) : LaunchEngineBridge(port, _bridgeError);
        if (launched) {
            _bridgeStartedAt = _services.clock ? _services.clock() : SteadySeconds();
        }
    }

    void ArtGenPanelModel::RefreshService(const ControlServerStatus& control, bool starting) {
        ArtGenSession& session = GetSession();
        ArtGenServiceView& view = _view.service;
        const ArtGenServiceStatus& status = session.GetServiceStatus();
        view.controlListening = control.listening;
        view.controlFailed = !control.listening && _bridgeWaitsForControl;
        view.connected = _connected;
        view.ready = session.IsGeneratorReady();
        view.startedHere = status.startedHere;
        view.setupBusy = session.IsSetupBusy();
        view.runBusy = session.IsBusy();
        SetText(view.startTip, control.listening ? kStartBridgeTip : kStartControlTip);
        if (!control.listening) {
            view.line = ArtGenServiceLine::ControlOff;
        } else if (!_connected) {
            view.line = starting ? ArtGenServiceLine::Starting : ArtGenServiceLine::NotConnected;
        } else {
            view.line = view.ready ? ArtGenServiceLine::Ready : ArtGenServiceLine::NotReady;
        }
        view.canStart = view.line == ArtGenServiceLine::ControlOff || view.line == ArtGenServiceLine::NotConnected;
        switch (view.line) {
            case ArtGenServiceLine::Starting:
                SetText(view.statusText, kStartingText);
                view.statusStyle = PanelTextStyle::Disabled;
                break;
            case ArtGenServiceLine::Ready:
                view.statusText = Format("Service ready ({})", status.generator);
                view.statusStyle = PanelTextStyle::Success;
                break;
            case ArtGenServiceLine::NotReady:
                view.statusText = Format("Generator {}", status.state);
                view.statusStyle = PanelTextStyle::Warning;
                break;
            default:
                SetText(view.statusText, kNotConnectedText);
                view.statusStyle = PanelTextStyle::Warning;
                break;
        }
        const bool didNotConnect = _bridgeStartedAt >= 0.0 && _bridgeError.empty();
        view.byHandText = didNotConnect ? Format("It did not connect: see {} in the editor's user folder, or run it by hand: python engine/{} --port {}", kBridgeLog, kArtGenBridgeScript, control.port)
                                        : Format("Or by hand: python engine/{} --port {}", kArtGenBridgeScript, control.port);
        view.bridgeError = _bridgeError;
        view.hasBridgeError = !_bridgeError.empty();
        view.hasReason = _connected && !view.ready && !status.reason.empty();
        view.reason = view.hasReason ? status.reason : String();
    }

    void ArtGenPanelModel::RefreshSetup() {
        ArtGenSession& session = GetSession();
        ArtGenSetupView& view = _view.setup;
        view = ArtGenSetupView{};
        view.canAct = !session.IsBusy();
        if (!_connected) {
            return;
        }
        const ArtGenSetup& setup = session.GetSetup();
        const StringView state(session.GetServiceStatus().state);
        view.busy = session.IsSetupBusy();
        if (view.busy) {
            SetText(view.doing, DoingOf(setup.action));
            view.current = setup.current;
            view.hasCurrent = !setup.current.empty();
            view.hasProgress = setup.total > 0;
            if (view.hasProgress) {
                view.progress = static_cast<float>(static_cast<double>(setup.done) / static_cast<double>(setup.total));
                view.progressText = Format("{:.0f} / {:.0f} MB", static_cast<double>(setup.done) / kBytesPerMegabyte, static_cast<double>(setup.total) / kBytesPerMegabyte);
            }
            view.log = setup.log;
            view.hasLog = !setup.log.empty();
            view.cancelShown = setup.action == ArtGenSetupAction::Install;
            view.cancelRequested = setup.cancelRequested;
            return;
        }
        view.failed = setup.state == ArtGenSetupState::Failed;
        if (view.failed) {
            view.error = setup.error;
        }
        view.hasDoneLog = setup.state == ArtGenSetupState::Done && setup.action != ArtGenSetupAction::Plan && !setup.log.empty();
        if (view.hasDoneLog) {
            view.doneLog = setup.log;
        }
        view.setupError = _setupError;
        view.hasSetupError = !_setupError.empty();
        view.planShown = setup.action == ArtGenSetupAction::Plan && setup.state == ArtGenSetupState::Done && state == kNotInstalled;
        view.notInstalled = !view.planShown && state == kNotInstalled;
        view.notRunning = state == kNotRunning;
        if (!view.planShown) {
            return;
        }
        view.platformText = Format("This machine: {}. {}", setup.platform, setup.platformNote);
        view.rootText = Format("Into {}:", setup.root);
        view.plan.reserve(setup.plan.size());
        for (const ArtGenSetupStep& step : setup.plan) {
            ArtGenPlanRow& row = view.plan.emplace_back();
            row.what = step.what;
            row.state = step.state;
            if (step.size > 0) {
                row.size = Format("{:.2f} GB", static_cast<double>(step.size) / kBytesPerGigabyte);
            }
            if (!step.licence.empty()) {
                row.tooltip = Format("Licence: {}\n{}", step.licence, step.dest);
            }
            view.fromSource = view.fromSource || StringView(step.state) == kInstallStep;
        }
        view.downloadSize = Format("{:.1f} GB", static_cast<double>(setup.downloadBytes) / kBytesPerGigabyte);
        view.downloadTip = Format("Download {}. Hover a row for its licence and where it goes. The download resumes if it stops", view.downloadSize);
    }

    bool ArtGenPanelModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kArtGenService)) {
            RunService(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenSetup)) {
            RunSetup(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenPreset)) {
            RunPreset(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenSaveAsOpened)) {
            OpenSaveAs();
            return true;
        }
        if (is(EditorCommandIds::kArtGenReference)) {
            RunReference(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenRun)) {
            RunRun(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenAccept)) {
            RunAccept(argument);
            return true;
        }
        if (is(EditorCommandIds::kArtGenListen)) {
            RunListen();
            return true;
        }
        return false;
    }

    void ArtGenPanelModel::RunService(StringView argument) {
        if (argument == ArtGenPanelArgs::kDisconnect) {
            GetSession().RequestServiceStop();
            return;
        }
        if (argument != ArtGenPanelArgs::kStart) {
            return;
        }
        const ControlServerStatus control = ControlStatus();
        if (control.listening) {
            StartBridge(control.port);
            return;
        }
        // The service talks to the editor over the control API, so Start turns it on first and the next Update starts the bridge once it listens
        _bridgeWaitsForControl = true;
        _stale = true;
        if (_services.startControl) {
            _services.startControl();
        } else {
            ApplicationEvents::StartControlServerRequestEvent::Emit();
        }
    }

    void ArtGenPanelModel::RunSetup(StringView argument) {
        ArtGenSession& session = GetSession();
        _stale = true;
        const auto request = [&](ArtGenSetupAction action) {
            auto requested = session.RequestSetup(action);
            _setupError = requested ? String() : requested.error();
        };
        if (argument == ArtGenPanelArgs::kPlan) {
            request(ArtGenSetupAction::Plan);
        } else if (argument == ArtGenPanelArgs::kInstall) {
            request(ArtGenSetupAction::Install);
        } else if (argument == ArtGenPanelArgs::kStart) {
            request(ArtGenSetupAction::Start);
        } else if (argument == ArtGenPanelArgs::kStop) {
            request(ArtGenSetupAction::Stop);
        } else if (argument == ArtGenPanelArgs::kCancel) {
            session.CancelSetup();
        } else if (argument == ArtGenPanelArgs::kDismiss) {
            _setupError.clear();
            session.ClearSetup();
        }
    }

}  // namespace BECore
