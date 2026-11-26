#include "ExportPanelModel.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <EASTL/algorithm.h>
#include <Games/GameManager.h>
#include <Generated/ExportPanelModel.gen.hpp>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>
#include <Widgets/AssetBrowser/AssetReferences.h>
#include <Widgets/Console/EditorLauncher.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/EngineTools.h>
#include <Widgets/Export/ExportScenes.h>

namespace BECore {

    namespace {

        constexpr StringView kLogName = "export.log";
        constexpr StringView kServeLogName = "export-serve.log";
        constexpr StringView kExportingProgressText = "Exporting";
        constexpr StringView kCancellingProgressText = "Cancelling the export";
        constexpr StringView kBlanks = " \t";

        std::expected<void, String> OpenWithSystem(StringView url) {
            const String text(url.data(), url.size());
            if (!SDL_OpenURL(text.c_str())) {
                return std::unexpected(String(SDL_GetError()));
            }
            return {};
        }

        void ReadRegisteredGameScenes(Vector<PoolString>& names) {
            for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                names.push_back(game->GetSceneName());
            }
        }

        /// The counts of @p check and what its rows do not show
        String SummaryOf(const ContentAssetCheck& check, size_t hidden) {
            String summary = Format("Missing: {}, unused: {}", check.missing.size(), check.unused.size());
            if (hidden > 0) {
                summary += Format(" ({} more not shown)", hidden);
            }
            if (!check.unchecked.empty()) {
                summary += ". Could not check: ";
                const size_t named = std::min(check.unchecked.size(), kMaxExportCheckNamedFiles);
                for (size_t i = 0; i < named; ++i) {
                    summary += Format("{}{}", i == 0 ? "" : ", ", check.unchecked[i]);
                }
                if (named < check.unchecked.size()) {
                    summary += Format(" and {} more", check.unchecked.size() - named);
                }
            }
            summary += ". Names built in code are not seen";
            return summary;
        }

        /// A new empty file at @p path. The old one is removed and not truncated: a cancelled export that is still ending writes on into its own
        std::expected<void, String> MakeEmptyLog(StringView path) {
            if (GetFileInfo(path)) {
                // A file that cannot be removed shows as the open that fails below
                static_cast<void>(RemoveFile(path));
            }
            if (const auto made = CreateDirectories(Path(path).Parent()); !made) {
                return made;
            }
            auto file = File::Open(path, FileMode::Write);
            if (!file) {
                return std::unexpected(std::move(file.error()));
            }
            return file->Close();
        }

        /// A pasted path often brings blanks along
        StringView Trimmed(StringView text) {
            const size_t first = text.find_first_not_of(kBlanks);
            if (first == StringView::npos) {
                return {};
            }
            return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
        }

    }  // namespace

    ExportPanelModel::ExportPanelModel() : _folderOpener(&OpenWithSystem), _gameScenes(&ReadRegisteredGameScenes) {
        SyncView();
    }

    void ExportPanelModel::Initialize() {
        _slots.Register();
        _progressSource = EditorProgressRegistry::GetInstance().Register([this] { return PollProgress(); });
    }

    TypedRef ExportPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ExportPanelModel::Update() {
        if (_editorStale) {
            RefreshFromEditor();
        }
        PollExport();
        if (_server) {
            if (const Optional<int> code = _server->Poll()) {
                _serveLog.Finish();
                FinishServer(*code);
            } else {
                _serveLog.Read();
            }
        }
        SyncView();
    }

    void ExportPanelModel::PollExport() {
        if (!_process) {
            return;
        }
        // Asked before the log is read: all a process wrote before it ended is in the file by now
        if (const Optional<int> code = _process->Poll()) {
            _log.Finish();
            Finish(*code);
        } else {
            _log.Read();
        }
    }

    Optional<EditorProgress> ExportPanelModel::PollProgress() {
        PollExport();
        if (!_process) {
            return {};
        }
        // The script's output has no reliable total, so there is no fraction
        return EditorProgress{_cancelRequested ? kCancellingProgressText : kExportingProgressText, {}};
    }

    void ExportPanelModel::OnOpenChanged(bool open) {
        if (open) {
            _editorStale = true;
        } else {
            _pointerOver = false;
        }
    }

    bool ExportPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kExportStart)) {
            Start();
        } else if (is(EditorCommandIds::kExportCancel)) {
            Cancel();
        } else if (is(EditorCommandIds::kExportOpenFolder)) {
            OpenFolder();
        } else if (is(EditorCommandIds::kExportRun)) {
            RunPackage();
        } else if (is(EditorCommandIds::kExportStop)) {
            StopServer();
        } else if (is(EditorCommandIds::kExportCheck)) {
            CheckAssets();
        } else {
            return false;
        }
        SyncView();
        return true;
    }

    void ExportPanelModel::Start() {
        if (_process) {
            return;
        }
        // The new package replaces the folder a server may be reading
        StopServer();
        _cancelRequested = false;
        _view.resultFolder.clear();
        _log.Follow({});
        RefreshFromEditor();
        CheckAssets();
        if (auto launched = Launch(); !launched) {
            _reason = std::move(launched.error());
            LOG_WARNING("Export: {}", _reason);
            SetState(ExportState::Failed);
            return;
        }
        SetState(ExportState::Running);
    }

    std::expected<void, String> ExportPanelModel::Launch() {
        const Optional<Path> script = FindEngineScript(Files().GetMountLayers(MountPoints::config), kExportScript);
        if (!script) {
            return std::unexpected(Format("no {} under the content roots (a packaged build cannot export)", kExportScript));
        }
        _script = String(script->View());
        const CommandLine& commandLine = _commandLine != nullptr ? *_commandLine : CoreManager::GetCommandLine();
        const auto executable = ResolveExportExecutable(commandLine.GetProgramPath());
        if (!executable) {
            return std::unexpected(executable.error());
        }
        const auto logPath = ExportLogPath();
        if (!logPath) {
            return std::unexpected(logPath.error());
        }
        if (auto emptied = MakeEmptyLog(*logPath); !emptied) {
            return emptied;
        }
        _log.Follow(*logPath);

        Vector<String> excluded;
        for (const ExportSceneRow& scene : _view.scenes) {
            if (!scene.included) {
                excluded.push_back(scene.name);
            }
        }
        _startedPlatform = _view.platform;
        const ExportRequest request{
            .executable = *executable, .config = _view.config, .platform = _startedPlatform, .outputDirectory = Trimmed(_view.outputDirectory), .excludedScenes = {excluded.data(), excluded.size()}};
        const Vector<String> arguments = BuildExportLaunch(EnginePython(), _script, request);
        auto process = Launcher().Start({arguments.data(), arguments.size()}, *logPath);
        if (!process) {
            return std::unexpected(std::move(process.error()));
        }
        _process = std::move(*process);
        return {};
    }

    void ExportPanelModel::Cancel() {
        if (!_process || _cancelRequested) {
            return;
        }
        _cancelRequested = true;
        _process->Cancel();
        _viewStale = true;
    }

    void ExportPanelModel::Finish(int exitCode) {
        _process.reset();
        if (exitCode == 0) {
            _view.resultFolder.assign(_log.GetResultFolder().data(), _log.GetResultFolder().size());
            SetState(ExportState::Succeeded);
        } else if (exitCode == kExportExitCancelled || _cancelRequested) {
            // A script that had to be killed has no exit code of its own
            SetState(ExportState::Cancelled);
        } else {
            _reason = _log.GetFailure().empty() ? Format("the script ended with exit code {}", exitCode) : String(_log.GetFailure());
            SetState(ExportState::Failed);
        }
    }

    void ExportPanelModel::OpenFolder() {
        if (!_view.hasResult) {
            return;
        }
        if (const auto opened = _folderOpener(FileUrlOf(_view.resultFolder)); !opened) {
            ShowProblem(Format("Could not open the folder: {}", opened.error()));
        } else {
            _viewStale = true;
        }
    }

    void ExportPanelModel::RunPackage() {
        if (!_view.hasResult) {
            return;
        }
        if (_startedPlatform == ExportPlatform::Web) {
            Serve();
            return;
        }
        const Vector<String> arguments = BuildPackageRunLaunch(EnginePython(), _script, _view.resultFolder);
        if (const auto started = Launcher().StartDetached({arguments.data(), arguments.size()}); !started) {
            ShowProblem(Format("Could not run the game: {}", started.error()));
        } else {
            _viewStale = true;
        }
    }

    void ExportPanelModel::Serve() {
        if (_server) {
            return;
        }
        const auto exportLog = ExportLogPath();
        if (!exportLog) {
            ShowProblem(Format("Could not serve the page: {}", exportLog.error()));
            return;
        }
        const String logPath((Path(*exportLog).Parent() / kServeLogName).View());
        if (const auto emptied = MakeEmptyLog(logPath); !emptied) {
            ShowProblem(Format("Could not serve the page: {}", emptied.error()));
            return;
        }
        _serveLog.Follow(logPath);
        const Vector<String> arguments = BuildPackageServeLaunch(EnginePython(), _script, _view.resultFolder);
        auto server = Launcher().Start({arguments.data(), arguments.size()}, logPath);
        if (!server) {
            ShowProblem(Format("Could not serve the page: {}", server.error()));
            return;
        }
        _server = std::move(*server);
        _stopRequested = false;
        _viewStale = true;
    }

    void ExportPanelModel::StopServer() {
        if (!_server || _stopRequested) {
            return;
        }
        _stopRequested = true;
        // The process is kept and polled until it ends: only `Poll` kills one that does not
        _server->Cancel();
    }

    void ExportPanelModel::FinishServer(int exitCode) {
        _server.reset();
        if (_stopRequested) {
            _stopRequested = false;
            return;
        }
        const StringView failure = _serveLog.GetFailure();
        ShowProblem(failure.empty() ? Format("The server ended with exit code {}", exitCode) : Format("The server stopped: {}", failure));
    }

    void ExportPanelModel::SetState(ExportState state) {
        _state = state;
        _viewStale = true;
    }

    void ExportPanelModel::ShowProblem(String problem) {
        // What is already due is made first, so the next SyncView does not write over the problem
        SyncView();
        LOG_WARNING("Export: {}", problem);
        _view.status = std::move(problem);
        _view.statusStyle = PanelTextStyle::Error;
    }

    void ExportPanelModel::SyncView() {
        const bool outputMoved = _log.GetRevision() != _shownRevision;
        if (outputMoved && (_log.GetErrorCount() != _view.errorCount || _log.GetWarningCount() != _view.warningCount)) {
            _view.errorCount = _log.GetErrorCount();
            _view.warningCount = _log.GetWarningCount();
            _viewStale = true;
        }
        _shownRevision = _log.GetRevision();
        if (!_viewStale) {
            SyncServerView();
            return;
        }
        _viewStale = false;
        _view.countsText = Format("Errors: {}, warnings: {}", _view.errorCount, _view.warningCount);
        _view.running = _state == ExportState::Running;
        _view.canStart = !_view.running;
        _view.canCancel = _view.running && !_cancelRequested;
        _view.hasResult = _state == ExportState::Succeeded && !_view.resultFolder.empty();
        SyncSceneRows();
        switch (_state) {
            case ExportState::Running:
                _view.status = _cancelRequested ? "Cancelling..." : "Exporting...";
                _view.statusStyle = PanelTextStyle::Normal;
                break;
            case ExportState::Succeeded:
                _view.status = "Export succeeded";
                _view.statusStyle = PanelTextStyle::Success;
                break;
            case ExportState::Failed:
                _view.status = Format("Export failed: {}", _reason);
                _view.statusStyle = PanelTextStyle::Error;
                break;
            case ExportState::Cancelled:
                _view.status = "Export cancelled";
                _view.statusStyle = PanelTextStyle::Warning;
                break;
            default:
                _view.status = "Nothing is exported yet";
                _view.statusStyle = PanelTextStyle::Disabled;
                break;
        }
        SyncServerView();
    }

    void ExportPanelModel::SyncServerView() {
        _view.serving = _server != nullptr;
        _view.canStop = _view.serving && !_stopRequested;
        _view.canRun = _view.hasResult && (_startedPlatform == ExportPlatform::Desktop || !_view.serving);
        const StringView url = _view.serving ? _serveLog.GetServeUrl() : StringView{};
        if (StringView(_view.serveUrl) != url) {
            _view.serveUrl.assign(url.data(), url.size());
        }
    }

    std::expected<String, String> ExportPanelModel::ExportLogPath() const {
        if (!_logPath.empty()) {
            return _logPath;
        }
        const Path userDirectory = EditorUserDirectory();
        if (userDirectory.View().empty()) {
            return std::unexpected(String("there is no editor user folder to write the log to"));
        }
        return String((userDirectory / kLogName).View());
    }

    void ExportPanelModel::RefreshFromEditor() {
        _editorStale = false;
        RefreshUnsaved();
        RefreshScenes();
    }

    void ExportPanelModel::RefreshUnsaved() {
        const EditorDocuments& documents = _documents != nullptr ? *_documents : CoreManager::GetEditorDocuments();
        _view.hasUnsaved = false;
        for (const EditorDocumentState& document : documents.Collect()) {
            _view.hasUnsaved = _view.hasUnsaved || document.dirty;
        }
    }

    void ExportPanelModel::RefreshScenes() {
        // A list that could not be read has no rows to take the choice from
        if (!_view.scenes.empty()) {
            _excluded.clear();
            for (const ExportSceneRow& row : _view.scenes) {
                if (!row.included) {
                    _excluded.push_back(row.name);
                }
            }
        }
        _view.scenes.clear();
        const Vector<Path> layers = Files().GetMountLayers(MountPoints::config);
        auto names = ReadExportScenes({layers.data(), layers.size()});
        _view.scenesUnread = !names;
        if (!names) {
            _view.scenesProblem = Format("The scenes cannot be listed ({}): every scene is exported", names.error());
            return;
        }
        Vector<PoolString> asked;
        _gameScenes(asked);
        for (String& name : *names) {
            const bool first = _view.scenes.empty();
            const bool isAsked = eastl::any_of(asked.begin(), asked.end(), [&name](PoolString scene) { return scene.ToStringView() == StringView(name); });
            ExportSceneRow& row = _view.scenes.emplace_back();
            row.locked = first || isAsked;
            row.included = row.locked || eastl::find(_excluded.begin(), _excluded.end(), name) == _excluded.end();
            if (first) {
                row.tooltip = "The game opens on the first scene, so it is always exported";
            } else if (isAsked) {
                row.tooltip = "The game asks for this scene, so it is always exported";
            }
            row.name = std::move(name);
        }
        SyncSceneRows();
    }

    void ExportPanelModel::SyncSceneRows() {
        for (ExportSceneRow& row : _view.scenes) {
            row.editable = !row.locked && _state != ExportState::Running;
        }
    }

    void ExportPanelModel::CheckAssets() {
        ContentAssetCheck check = CheckContentAssets(Files());
        const size_t total = check.missing.size() + check.unused.size();
        const size_t shown = std::min(total, kMaxExportCheckRows);
        _view.findings.clear();
        _view.findings.reserve(shown);
        for (const MissingAsset& missing : check.missing) {
            if (_view.findings.size() == shown) {
                break;
            }
            ExportCheckRow& row = _view.findings.emplace_back();
            row.problem = "Missing";
            row.path = missing.name;
            row.where = Format("{}:{}", missing.use.file, missing.use.line);
            row.style = PanelTextStyle::Error;
        }
        for (const String& unused : check.unused) {
            if (_view.findings.size() == shown) {
                break;
            }
            ExportCheckRow& row = _view.findings.emplace_back();
            row.problem = "Unused";
            row.path = unused;
            row.style = PanelTextStyle::Warning;
        }
        _view.checked = true;
        _view.hasFindings = !_view.findings.empty();
        _view.checkSummary = SummaryOf(check, total - shown);
    }

    const FileSystem& ExportPanelModel::Files() const {
        return _fileSystem != nullptr ? *_fileSystem : CoreManager::GetFileSystem();
    }

    IExportLauncher& ExportPanelModel::Launcher() {
        if (_launcher != nullptr) {
            return *_launcher;
        }
        if (!_ownLauncher) {
            _ownLauncher = MakeExportLauncher();
        }
        return *_ownLauncher;
    }

}  // namespace BECore
