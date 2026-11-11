#include "ProfilerSettings.h"

namespace BECore {

    namespace {

        /// The flag name without its '=' ("--trace" of "--trace=")
        eastl::string_view BareFlag(eastl::string_view prefix) {
            return prefix.substr(0, prefix.size() - 1);
        }

        std::optional<ProfilerBackendKind> ParseBackend(eastl::string_view value) {
            if (value == ProfilerArguments::kNone) {
                return ProfilerBackendKind::None;
            }
            if (value == ProfilerArguments::kChrome) {
                return ProfilerBackendKind::ChromeTrace;
            }
            if (value == ProfilerArguments::kTracy) {
                return ProfilerBackendKind::Tracy;
            }
            return std::nullopt;
        }

    }  // namespace

    std::expected<ProfilerSettings, eastl::string> ResolveProfilerSettings(eastl::span<const eastl::string> args, ProfilerBuild build) {
        using namespace ProfilerArguments;

        std::optional<ProfilerBackendKind> asked;
        eastl::string tracePath;
        bool anyFlag = false;
        for (const eastl::string& arg : args) {
            const eastl::string_view argument(arg.data(), arg.size());
            if (argument.starts_with(kTracePrefix)) {
                anyFlag = true;
                tracePath.assign(argument.substr(kTracePrefix.size()).data(), argument.size() - kTracePrefix.size());
                if (tracePath.empty()) {
                    return std::unexpected(Format("'{}' needs a file name: {}<file>", argument, kTracePrefix));
                }
            } else if (argument.starts_with(kProfilerPrefix)) {
                anyFlag = true;
                const eastl::string_view value = argument.substr(kProfilerPrefix.size());
                asked = ParseBackend(value);
                if (!asked) {
                    return std::unexpected(Format("'{}': the profiler is one of {}, {}, {}", argument, kNone, kChrome, kTracy));
                }
            } else if (argument == BareFlag(kTracePrefix) || argument == BareFlag(kProfilerPrefix)) {
                return std::unexpected(Format("'{}' needs a value: {}<value>", argument, argument));
            }
        }

        if (anyFlag && !build.profiler) {
            return std::unexpected(eastl::string("the profiler is not in this build: configure with ENGINE_PROFILER=ON (preset macos-profile)"));
        }

        ProfilerSettings settings;
        settings.backend = build.tracy ? ProfilerBackendKind::Tracy : ProfilerBackendKind::None;
        if (!tracePath.empty()) {
            if (asked && *asked != ProfilerBackendKind::ChromeTrace) {
                return std::unexpected(Format("{}<file> writes a Chrome trace: it cannot go with {}{}", kTracePrefix, kProfilerPrefix, *asked == ProfilerBackendKind::None ? kNone : kTracy));
            }
            settings.backend = ProfilerBackendKind::ChromeTrace;
            settings.tracePath = std::move(tracePath);
        } else if (asked) {
            settings.backend = *asked;
        }
        if (settings.backend == ProfilerBackendKind::Tracy && !build.tracy) {
            return std::unexpected(Format("{}{}: Tracy is not in this build: configure with ENGINE_PROFILER_TRACY=ON (preset macos-profile-tracy)", kProfilerPrefix, kTracy));
        }
        return settings;
    }

}  // namespace BECore
