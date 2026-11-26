#include "ExportPanelModel.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Generated/ExportPanelModel.gen.hpp>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>
#include <Widgets/Console/EditorLauncher.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/EngineTools.h>

namespace BECore {

    namespace {

        constexpr StringView kLogName = "export.log";
        constexpr StringView kBlanks = " \t";

        std::expected<void, String> OpenWithSystem(StringView url) {
            const String text(url.data(), url.size());
            if (!SDL_OpenURL(text.c_str())) {
                return std::unexpected(String(SDL_GetError()));
            }
            return {};
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

    ExportPanelModel::ExportPanelModel() : _folderOpener(&OpenWithSystem) {
        SyncView();
    }

    void ExportPanelModel::Initialize() {
        _slots.Register();
    }

    TypedRef ExportPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ExportPanelModel::Update() {
        if (_documentsStale) {
            RefreshUnsaved();
        }
        if (_process) {
            // Asked before the log is read: all a process wrote before it ended is in the file by now
            if (const Optional<int> code = _process->Poll()) {
                _log.Finish();
                Finish(*code);
            } else {
                _log.Read();
            }
        }
        SyncView();
    }

    void ExportPanelModel::OnOpenChanged(bool open) {
        if (open) {
            _documentsStale = true;
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
        _cancelRequested = false;
        _view.resultFolder.clear();
        _log.Follow({});
        RefreshUnsaved();
        if (auto launched = Launch(); !launched) {
            _reason = std::move(launched.error());
            LOG_WARNING("Export: {}", _reason);
            SetState(ExportState::Failed);
            return;
        }
        SetState(ExportState::Running);
    }

    std::expected<void, String> ExportPanelModel::Launch() {
        const FileSystem& files = _fileSystem != nullptr ? *_fileSystem : CoreManager::GetFileSystem();
        const Optional<Path> script = FindEngineScript(files.GetMountLayers(MountPoints::config), kExportScript);
        if (!script) {
            return std::unexpected(Format("no {} under the content roots (a packaged build cannot export)", kExportScript));
        }
        _script = String(script->View());
        const CommandLine& commandLine = _commandLine != nullptr ? *_commandLine : CoreManager::GetCommandLine();
        const auto executable = ResolveExportExecutable(commandLine.GetProgramPath());
        if (!executable) {
            return std::unexpected(executable.error());
        }
        String logPath = _logPath;
        if (logPath.empty()) {
            const Path userDirectory = EditorUserDirectory();
            if (userDirectory.View().empty()) {
                return std::unexpected(String("there is no editor user folder to write the log to"));
            }
            logPath = String((userDirectory / kLogName).View());
        }
        if (auto emptied = MakeEmptyLog(logPath); !emptied) {
            return emptied;
        }
        _log.Follow(logPath);

        const Vector<String> arguments = BuildExportLaunch(EnginePython(), _script, *executable, _view.config, Trimmed(_view.outputDirectory));
        auto process = Launcher().Start({arguments.data(), arguments.size()}, logPath);
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
        const Vector<String> arguments = BuildPackageRunLaunch(EnginePython(), _script, _view.resultFolder);
        if (const auto started = Launcher().StartDetached({arguments.data(), arguments.size()}); !started) {
            ShowProblem(Format("Could not run the game: {}", started.error()));
        } else {
            _viewStale = true;
        }
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
            return;
        }
        _viewStale = false;
        _view.countsText = Format("Errors: {}, warnings: {}", _view.errorCount, _view.warningCount);
        _view.running = _state == ExportState::Running;
        _view.canStart = !_view.running;
        _view.canCancel = _view.running && !_cancelRequested;
        _view.hasResult = _state == ExportState::Succeeded && !_view.resultFolder.empty();
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
    }

    void ExportPanelModel::RefreshUnsaved() {
        _documentsStale = false;
        const EditorDocuments& documents = _documents != nullptr ? *_documents : CoreManager::GetEditorDocuments();
        _view.hasUnsaved = false;
        for (const EditorDocumentState& document : documents.Collect()) {
            _view.hasUnsaved = _view.hasUnsaved || document.dirty;
        }
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
