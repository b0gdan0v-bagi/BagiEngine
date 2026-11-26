#pragma once

#include <BECore/Application/CommandLine.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/Export/ExportPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// What a fake process was told, and what it answers
    struct FakeExportRun {
        Optional<int> exitCode;
        bool cancelled = false;
        bool alive = false;
    };

    class FakeExportProcess final : public IExportProcess {
    public:
        explicit FakeExportProcess(FakeExportRun& run) : _run(run) {
            _run.alive = true;
        }

        ~FakeExportProcess() override {
            // The contract of the interface: a running process is cancelled by its destruction
            _run.cancelled = _run.cancelled || !_run.exitCode;
            _run.alive = false;
        }

        Optional<int> Poll() override {
            return _run.exitCode;
        }

        void Cancel() override {
            _run.cancelled = true;
        }

    private:
        FakeExportRun& _run;
    };

    /// Starts nothing: keeps what it was asked to start, and one run at a time
    class FakeExportLauncher final : public IExportLauncher {
    public:
        std::expected<UniquePtr<IExportProcess>, String> Start(Span<const String> arguments, StringView log) override {
            if (!refusal.empty()) {
                return std::unexpected(refusal);
            }
            ++starts;
            started.assign(arguments.begin(), arguments.end());
            logPath.assign(log.data(), log.size());
            run = {};
            return UniquePtr<IExportProcess>(MakeUnique<FakeExportProcess>(run));
        }

        std::expected<void, String> StartDetached(Span<const String> arguments) override {
            if (!refusal.empty()) {
                return std::unexpected(refusal);
            }
            ++detachedStarts;
            detached.assign(arguments.begin(), arguments.end());
            return {};
        }

        /// Appends @p text to the log, as the running process would
        void Write(StringView text) const {
            auto file = File::Open(logPath, FileMode::Append);
            ASSERT_TRUE(file.has_value()) << file.error().c_str();
            ASSERT_TRUE(file->Write(text).has_value());
        }

        /// The process wrote @p text and ended with @p exitCode
        void End(int exitCode, StringView text = {}) {
            Write(text);
            run.exitCode = exitCode;
        }

        String refusal;
        Vector<String> started;
        Vector<String> detached;
        String logPath;
        FakeExportRun run;
        int starts = 0;
        int detachedStarts = 0;
    };

    /// Everything the Export window's model takes from outside: a program, a content root that holds the script, a log of its own, fakes
    class ExportFixture {
    public:
        explicit ExportFixture(const String& prefix) : folder(prefix) {
            program = folder.Write("Game", "x");
            commandLine.SetProgramPath(program);
            const Path root = Path(folder.PathOf("engine"));
            script = String((root / kExportScript).View());
            WriteFile(root / kExportScript, "");
            MakeDirectory(root / "config");
            files.AddContentRoot(root);
            files.Initialize();
            logPath = folder.PathOf("user/export.log");
            documentsHandle = documents.Add([this](Vector<EditorDocumentState>& states) {
                EditorDocumentState state;
                state.id = "scene:probe";
                state.dirty = dirty;
                states.push_back(std::move(state));
            });
        }

        void Apply(ExportPanelModel& model) {
            model.SetLauncher(launcher);
            model.SetLogPath(logPath);
            model.SetCommandLine(commandLine);
            model.SetFileSystem(files);
            model.SetDocuments(documents);
            model.SetFolderOpener([this](StringView url) -> std::expected<void, String> {
                openedUrl.assign(url.data(), url.size());
                if (!openerRefusal.empty()) {
                    return std::unexpected(openerRefusal);
                }
                return {};
            });
        }

        TestTempFolder folder;
        String program;
        String script;
        String logPath;
        String openedUrl;
        String openerRefusal;
        bool dirty = false;
        CommandLine commandLine;
        FileSystem files;
        EditorDocuments documents;
        EditorDocumentsHandle documentsHandle;
        FakeExportLauncher launcher;
    };

}  // namespace BECore::Tests
