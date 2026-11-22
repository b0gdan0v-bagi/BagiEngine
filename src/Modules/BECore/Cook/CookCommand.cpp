#include "CookCommand.h"

#include <BECore/Cook/ContentCook.h>
#include <BECore/Cook/ContentCooker.h>
#include <BECore/Cook/ContentVerify.h>
#include <cstdio>

namespace BECore {

    namespace {

        constexpr StringView kCookPrefix = "--cook=";
        constexpr StringView kCookFlag = "--cook";
        constexpr StringView kVerifyFlag = "--cook-verify";

        void PrintToConsole(StringView text) {
            std::fwrite(text.data(), 1, text.size(), stdout);  //-NoStd: the report is the program's output, the logger's sinks are not up yet
            std::fflush(stdout);
        }

    }  // namespace

    std::expected<Optional<CookRequest>, String> ParseCookArguments(Span<const String> arguments) {
        Optional<CookRequest> request;
        bool verify = false;
        for (const String& argument : arguments) {
            const StringView view(argument.data(), argument.size());
            if (view.starts_with(kCookPrefix)) {
                if (request.has_value()) {
                    return std::unexpected(Format("'{}': {} is given twice", view, kCookFlag));
                }
                const StringView value = view.substr(kCookPrefix.size());
                if (value.empty()) {
                    return std::unexpected(Format("'{}' needs a folder: {}<dir>", view, kCookPrefix));
                }
                request = CookRequest{String(value), false};
            } else if (view == kCookFlag) {
                return std::unexpected(Format("'{}' needs a folder: {}<dir>", view, kCookPrefix));
            } else if (view == kVerifyFlag) {
                verify = true;
            }
        }
        if (verify && !request.has_value()) {
            return std::unexpected(Format("{} needs {}<dir>: it checks the files a cook wrote", kVerifyFlag, kCookPrefix));
        }
        if (request.has_value()) {
            request->verify = verify;
        }
        return request;
    }

    int RunCookCommand(const CookRequest& request, const FileSystem& fs) {
        const std::filesystem::path outDir(ToStd(StringView(request.outDir.data(), request.outDir.size())));
        const ContentCookRegistry& registry = ContentCookRegistry::GetInstance();

        const CookReport cooked = CookContent(fs, outDir, registry);
        PrintToConsole(cooked.ToText());
        if (!cooked.Ok()) {
            return kCookExitFailed;
        }
        if (!request.verify) {
            return 0;
        }

        const CookReport verified = VerifyCooked(fs, outDir, registry);
        PrintToConsole("verify (cooked = read back equal to its source):\n");
        PrintToConsole(verified.ToText());
        return verified.Ok() ? 0 : kCookExitFailed;
    }

}  // namespace BECore
