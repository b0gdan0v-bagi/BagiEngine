#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/Export/ExportLaunch.h>
#include <Widgets/Export/ExportLog.h>
#include <Widgets/Export/ExportSlots.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class CommandLine;
    class EditorDocuments;
    class FileSystem;

    CORE_ENUM(ExportState, uint8_t, Idle, Running, Succeeded, Failed, Cancelled)

    /// The lines of a server's output the model keeps: it needs the address and the reason of an end
    inline constexpr size_t kMaxExportServeLogLines = 100;

    /// The rows of findings the panel lists at most; the summary counts the others
    inline constexpr size_t kMaxExportCheckRows = 200;

    /// The files a check could not read that its summary names; it counts the others
    inline constexpr size_t kMaxExportCheckNamedFiles = 5;

    /// A scene of the game, as the list of the panel shows it
    struct ExportSceneRow {
        BE_CLASS(ExportSceneRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD bool included = true;
        /// The game opens on it or asks for it by name, so it cannot be left out
        BE_REFLECT_FIELD bool locked = false;
        /// The choice can be changed now: not locked and no export runs
        BE_REFLECT_FIELD bool editable = true;
        /// Why a locked scene cannot be left out; empty otherwise
        BE_REFLECT_FIELD String tooltip;
    };

    /// A finding of the check of the assets
    struct ExportCheckRow {
        BE_CLASS(ExportCheckRow)

        BE_REFLECT_FIELD String problem;
        BE_REFLECT_FIELD String path;
        /// The file and the line that name a missing asset; empty for an unused file
        BE_REFLECT_FIELD String where;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Normal;
    };

    /**
     * @brief What the Export window's asset binds to
     *
     * `platform`, `config`, `outputDirectory`, `errorsOnly` and the `included` of a scene are written by the controls of the panel; the rest is written
     * by the model. The choices are read when an export starts. A condition of an asset is one bool or one comparison, so every state the panel shows
     * is a field of its own.
     */
    struct ExportPanelView {
        BE_CLASS(ExportPanelView)

        BE_REFLECT_FIELD ExportPlatform platform = ExportPlatform::Desktop;
        BE_REFLECT_FIELD ExportConfig config = ExportConfig::Debug;
        /// The folder of the package; empty is the script's default
        BE_REFLECT_FIELD String outputDirectory;
        BE_REFLECT_FIELD String status;
        BE_REFLECT_FIELD PanelTextStyle statusStyle = PanelTextStyle::Normal;
        BE_REFLECT_FIELD bool running = false;
        BE_REFLECT_FIELD bool canStart = true;
        BE_REFLECT_FIELD bool canCancel = false;
        /// The last export succeeded, so there is a package to open and to run
        BE_REFLECT_FIELD bool hasResult = false;
        BE_REFLECT_FIELD String resultFolder;
        /// There is a result that can be run now: a web one cannot while a server runs
        BE_REFLECT_FIELD bool canRun = false;
        /// A server of a web package runs, one that was told to stop too
        BE_REFLECT_FIELD bool serving = false;
        BE_REFLECT_FIELD bool canStop = false;
        /// The address the server listens on; empty until it says it
        BE_REFLECT_FIELD String serveUrl;
        BE_REFLECT_FIELD bool errorsOnly = false;
        BE_REFLECT_FIELD uint32_t errorCount = 0;
        BE_REFLECT_FIELD uint32_t warningCount = 0;
        BE_REFLECT_FIELD String countsText;
        /// An editor document has changes that are not saved: the export reads the saved files
        BE_REFLECT_FIELD bool hasUnsaved = false;
        BE_REFLECT_FIELD Vector<ExportSceneRow> scenes;
        /// The scenes cannot be listed, so none can be left out; `scenesProblem` says why
        BE_REFLECT_FIELD bool scenesUnread = false;
        BE_REFLECT_FIELD String scenesProblem;
        /// The assets were checked at least once: `checkSummary` and `findings` are of the last check
        BE_REFLECT_FIELD bool checked = false;
        BE_REFLECT_FIELD String checkSummary;
        /// Missing assets first, then unused files
        BE_REFLECT_FIELD Vector<ExportCheckRow> findings;
        BE_REFLECT_FIELD bool hasFindings = false;
    };

    /**
     * @brief The model of the Export window: starts the export, follows its output and offers the result, with no ImGui
     *
     * One export at a time. The process runs on while the panel is closed; its end and the rest of its output are taken at the next `Update`. Destroying
     * the model (a switch to Game mode) cancels it. The exit code of the script decides the state. A steady `Update` with no new output allocates nothing.
     * The documents are asked for unsaved changes and the scenes are listed when the panel opens, when the pointer comes back to it and when an export
     * starts. The assets are checked on command and at each start; a finding never stops an export.
     *
     * Running a web result starts a server, a second process with a log of its own: `export.stop`, a new export and the model's end stop it, closing
     * the panel does not.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `export.start`, `export.cancel`, `export.openFolder`, `export.run`,
     * `export.stop` and `export.check`.
     */
    class ExportPanelModel : public IPanelModel {
        BE_CLASS(ExportPanelModel)
    public:
        /// Shows the folder a `file://` URL names
        using FolderOpener = MoveOnlyFunction<std::expected<void, String>(StringView url)>;
        /// Adds the scenes the registered games ask for
        using GameScenes = MoveOnlyFunction<void(Vector<PoolString>& names)>;

        ExportPanelModel();

        /// Registers the slot that draws the output; called once by the widget that hosts the model
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Starts processes with @p launcher instead of SDL; it must outlive the model
        void SetLauncher(IExportLauncher& launcher) {
            _launcher = &launcher;
        }

        /// Writes the output to @p path instead of `export.log` in the editor's user folder; a server's output goes beside it
        void SetLogPath(StringView path) {
            _logPath.assign(path.data(), path.size());
        }

        /// Takes the program to export from @p commandLine instead of CoreManager's; it must outlive the model
        void SetCommandLine(const CommandLine& commandLine) {
            _commandLine = &commandLine;
        }

        /// Reads the content of @p fileSystem (the script, the scenes, the checked assets) instead of CoreManager's; it must outlive the model
        void SetFileSystem(const FileSystem& fileSystem) {
            _fileSystem = &fileSystem;
            _editorStale = true;
        }

        /// Asks @p documents for unsaved changes instead of CoreManager's; it must outlive the model
        void SetDocuments(const EditorDocuments& documents) {
            _documents = &documents;
            _editorStale = true;
        }

        void SetFolderOpener(FolderOpener opener) {
            _folderOpener = std::move(opener);
        }

        /// Asks @p scenes for the scenes of the games instead of GameManager
        void SetGameScenes(GameScenes scenes) {
            _gameScenes = std::move(scenes);
            _editorStale = true;
        }

        /// Whether the pointer is over the panel, told every frame it is drawn: coming back to it, the user may have saved or edited elsewhere
        void NotePointerOver(bool over) {
            _editorStale = _editorStale || (over && !_pointerOver);
            _pointerOver = over;
        }

        [[nodiscard]] const ExportPanelView& GetView() const {
            return _view;
        }

        [[nodiscard]] ExportState GetState() const {
            return _state;
        }

        /// The output of the running or the last export
        [[nodiscard]] const ExportLog& GetLog() const {
            return _log;
        }

    private:
        void Start();
        /// What `Start` does once nothing runs; the error is why no process was started
        [[nodiscard]] std::expected<void, String> Launch();
        void Cancel();
        void OpenFolder();
        void RunPackage();
        /// Starts the server of the web result
        void Serve();
        /// Asks the server to end; `Update` takes the end
        void StopServer();
        void FinishServer(int exitCode);
        void Finish(int exitCode);
        void SetState(ExportState state);
        /// A problem with the result, shown in the status line until the state changes
        void ShowProblem(String problem);
        /// Makes the strings and the counts again when the output or the state moved
        void SyncView();
        void SyncServerView();
        /// The file the export writes its output to; an error when there is no folder for it
        [[nodiscard]] std::expected<String, String> ExportLogPath() const;
        /// Reads again what the user may have changed elsewhere in the editor: the unsaved documents and the scenes
        void RefreshFromEditor();
        void RefreshUnsaved();
        /// Lists the scenes again, keeping the choice by name
        void RefreshScenes();
        /// A scene can be changed only while no export runs
        void SyncSceneRows();
        void CheckAssets();
        [[nodiscard]] const FileSystem& Files() const;
        [[nodiscard]] IExportLauncher& Launcher();

        ExportPanelView _view;
        ExportLog _log;
        ExportLog _serveLog{kMaxExportServeLogLines};
        ExportSlots _slots{*this};
        String _logPath;
        /// The export script the last export was started with
        String _script;
        /// Why the export failed
        String _reason;
        FolderOpener _folderOpener;
        GameScenes _gameScenes;
        /// The scenes the user left out, kept while the list cannot be read
        Vector<String> _excluded;
        IExportLauncher* _launcher = nullptr;
        UniquePtr<IExportLauncher> _ownLauncher;
        UniquePtr<IExportProcess> _process;  // after the launchers: it dies first
        UniquePtr<IExportProcess> _server;   // after the launchers too
        const CommandLine* _commandLine = nullptr;
        const FileSystem* _fileSystem = nullptr;
        const EditorDocuments* _documents = nullptr;
        ExportState _state = ExportState::Idle;
        /// What the last export was started for; the choice of the view may have moved since
        ExportPlatform _startedPlatform = ExportPlatform::Desktop;
        bool _stopRequested = false;
        uint32_t _shownRevision = 0;
        bool _viewStale = true;
        bool _cancelRequested = false;
        bool _editorStale = true;
        bool _pointerOver = false;
    };

}  // namespace BECore
