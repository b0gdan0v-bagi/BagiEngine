#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// The exit codes of `<exe> --cook=<dir>`: 0 for a cook (and verify) with no failure
    inline constexpr int kCookExitFailed = 1;  ///< A file failed to cook or to verify
    inline constexpr int kCookExitUsage = 2;   ///< The arguments are wrong; nothing was cooked

    /// What `<exe> --cook=<dir> [--cook-verify]` asks for
    struct CookRequest {
        String outDir;        ///< `--cook=<dir>`: where `layer<k>/config/` are written
        bool verify = false;  ///< `--cook-verify`: read the result back and compare it with the source (`VerifyCooked`)
    };

    /**
     * @brief The cook request among the program's arguments (without the program's name)
     *
     * No `--cook` and no `--cook-verify` is "no request" (`nullopt`); other flags are not the cook's and are ignored, as every reader of
     * `CommandLine` does. The error is the message for the console: an empty or missing value, `--cook` given twice, or
     * `--cook-verify` without `--cook` (a program that verifies nothing). The caller exits with `kCookExitUsage`.
     */
    std::expected<Optional<CookRequest>, String> ParseCookArguments(Span<const String> arguments);

    /**
     * @brief Cooks the content of @p fs with the process registry of cookers, verifies it when asked, prints the report to stdout
     *
     * The report is `ToText()` of the cook and, with `request.verify`, of `VerifyCooked`, which runs only after a cook with no failure
     * (a file that did not cook has nothing to compare). Needs the file system and the task manager, which `ConfigManager` loads on: the
     * engine's own `Application::RunCook` brings both up, a test has them from `EngineEnvironment`. Takes no window, GPU or control server.
     *
     * @return 0, or `kCookExitFailed` when any file failed
     */
    int RunCookCommand(const CookRequest& request, const FileSystem& fs);

}  // namespace BECore
