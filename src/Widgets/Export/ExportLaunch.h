#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The export script, relative to the engine checkout; its command line, output and exit codes: `tools/export/CLAUDE.md`
    inline constexpr StringView kExportScript = "tools/export/export.py";

    /// The script's exit code of an export that was cancelled
    inline constexpr int kExportExitCancelled = 3;

    /// How long a cancelled script has to end before it is killed. It has to be longer than the grace the script gives its own build
    /// (`KILL_GRACE_SECONDS`): killing the script first would leave the build running
    inline constexpr uint64_t kExportKillGraceMs = 15000;

    CORE_ENUM(ExportConfig, uint8_t, Debug, Release)

    CORE_ENUM(ExportPlatform, uint8_t, Desktop, Web)

    /// What one export is asked for
    struct ExportRequest {
        /// The running program, which names the game to export
        StringView executable;
        ExportConfig config = ExportConfig::Debug;
        ExportPlatform platform = ExportPlatform::Desktop;
        /// Empty leaves the script's default
        StringView outputDirectory;
        /// The scenes the package goes without
        Span<const String> excludedScenes;
    };

    [[nodiscard]] Vector<String> BuildExportLaunch(StringView python, StringView script, const ExportRequest& request);

    /// The arguments that start the game of an exported desktop package
    [[nodiscard]] Vector<String> BuildPackageRunLaunch(StringView python, StringView script, StringView packageDirectory);

    /// The arguments that serve an exported web package and open its page in the browser. The process lives until it is cancelled
    [[nodiscard]] Vector<String> BuildPackageServeLaunch(StringView python, StringView script, StringView packageDirectory);

    /// @p programPath (`CommandLine::GetProgramPath`) made absolute, for the script's `--exe`. An error sentence when no file is there: a program
    /// started by a bare name from PATH cannot say where it is
    [[nodiscard]] std::expected<String, String> ResolveExportExecutable(StringView programPath);

    /// A process of the script that was started: an export or a server. Destroying a running one cancels it and does not wait for it
    class IExportProcess {
    public:
        virtual ~IExportProcess() = default;

        /// The exit code once the process has ended (negative: a signal ended it), nullopt while it runs. Never blocks. It is also what kills
        /// a cancelled process that outlives its grace, so a cancel that has to be sure is polled
        [[nodiscard]] virtual Optional<int> Poll() = 0;

        /// Asks the process to give up; `Poll` tells when it has
        virtual void Cancel() = 0;
    };

    /// Starts the processes of the Export panel (a test sets its own)
    class IExportLauncher {
    public:
        virtual ~IExportLauncher() = default;

        /// Starts @p arguments with the standard output and the standard error written to the file @p log, which is replaced
        [[nodiscard]] virtual std::expected<UniquePtr<IExportProcess>, String> Start(Span<const String> arguments, StringView log) = 0;

        /// Starts @p arguments with no standard streams and forgets them: nothing tells how they ended
        [[nodiscard]] virtual std::expected<void, String> StartDetached(Span<const String> arguments) = 0;
    };

    /// The launcher over SDL processes; @p killGraceMs is `kExportKillGraceMs` outside a test
    [[nodiscard]] UniquePtr<IExportLauncher> MakeExportLauncher(uint64_t killGraceMs = kExportKillGraceMs);

}  // namespace BECore
