#pragma once

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

    /**
     * @brief What the Export window's asset binds to
     *
     * `config`, `outputDirectory` and `errorsOnly` are written by the controls of the panel; the rest is written by the model. The two choices are read when
     * an export starts. A condition of an asset is one bool or one comparison, so every state the panel shows is a field of its own.
     */
    struct ExportPanelView {
        BE_CLASS(ExportPanelView)

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
        BE_REFLECT_FIELD bool errorsOnly = false;
        BE_REFLECT_FIELD uint32_t errorCount = 0;
        BE_REFLECT_FIELD uint32_t warningCount = 0;
        BE_REFLECT_FIELD String countsText;
        /// An editor document has changes that are not saved: the export reads the saved files
        BE_REFLECT_FIELD bool hasUnsaved = false;
    };

    /**
     * @brief The model of the Export window: starts the export, follows its output and offers the result, with no ImGui
     *
     * One export at a time. The process runs on while the panel is closed; its end and the rest of its output are taken at the next `Update`. Destroying
     * the model (a switch to Game mode) cancels it. The exit code of the script decides the state. A steady `Update` with no new output allocates nothing.
     * The documents are asked for unsaved changes when the panel opens, when the pointer comes back to it and when an export starts.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `export.start`, `export.cancel`, `export.openFolder` and `export.run`.
     */
    class ExportPanelModel : public IPanelModel {
        BE_CLASS(ExportPanelModel)
    public:
        /// Shows the folder a `file://` URL names
        using FolderOpener = MoveOnlyFunction<std::expected<void, String>(StringView url)>;

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

        /// Writes the output to @p path instead of `export.log` in the editor's user folder
        void SetLogPath(StringView path) {
            _logPath.assign(path.data(), path.size());
        }

        /// Takes the program to export from @p commandLine instead of CoreManager's; it must outlive the model
        void SetCommandLine(const CommandLine& commandLine) {
            _commandLine = &commandLine;
        }

        /// Looks for the script among the content roots of @p fileSystem instead of CoreManager's; it must outlive the model
        void SetFileSystem(const FileSystem& fileSystem) {
            _fileSystem = &fileSystem;
        }

        /// Asks @p documents for unsaved changes instead of CoreManager's; it must outlive the model
        void SetDocuments(const EditorDocuments& documents) {
            _documents = &documents;
            _documentsStale = true;
        }

        void SetFolderOpener(FolderOpener opener) {
            _folderOpener = std::move(opener);
        }

        /// Whether the pointer is over the panel, told every frame it is drawn: coming back to it, the user may have saved or edited elsewhere
        void NotePointerOver(bool over) {
            _documentsStale = _documentsStale || (over && !_pointerOver);
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
        void Finish(int exitCode);
        void SetState(ExportState state);
        /// A problem with the result, shown in the status line until the state changes
        void ShowProblem(String problem);
        /// Makes the strings and the counts again when the output or the state moved
        void SyncView();
        void RefreshUnsaved();
        [[nodiscard]] IExportLauncher& Launcher();

        ExportPanelView _view;
        ExportLog _log;
        ExportSlots _slots{*this};
        String _logPath;
        /// The export script the last export was started with
        String _script;
        /// Why the export failed
        String _reason;
        FolderOpener _folderOpener;
        IExportLauncher* _launcher = nullptr;
        UniquePtr<IExportLauncher> _ownLauncher;
        UniquePtr<IExportProcess> _process;  // after the launchers: it dies first
        const CommandLine* _commandLine = nullptr;
        const FileSystem* _fileSystem = nullptr;
        const EditorDocuments* _documents = nullptr;
        ExportState _state = ExportState::Idle;
        uint32_t _shownRevision = 0;
        bool _viewStale = true;
        bool _cancelRequested = false;
        bool _documentsStale = true;
        bool _pointerOver = false;
    };

}  // namespace BECore
