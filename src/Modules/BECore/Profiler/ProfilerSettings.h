#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <expected>

namespace BECore {

    /// The backend the BE_PROFILE_* macros send to: none, the Chrome trace recorder of the engine, or the vendored Tracy client
    CORE_ENUM(ProfilerBackendKind, uint8_t, None, ChromeTrace, Tracy)

    /// Flags of the command line that Application hands to ResolveProfilerSettings()
    namespace ProfilerArguments {
        inline constexpr eastl::string_view kTracePrefix = "--trace=";
        inline constexpr eastl::string_view kProfilerPrefix = "--profiler=";
        inline constexpr eastl::string_view kNone = "none";
        inline constexpr eastl::string_view kChrome = "chrome";
        inline constexpr eastl::string_view kTracy = "tracy";
    }  // namespace ProfilerArguments

    /// What this build can profile (the CMake options ENGINE_PROFILER and ENGINE_PROFILER_TRACY)
    struct ProfilerBuild {
        bool profiler = false;
        bool tracy = false;
    };

    inline constexpr ProfilerBuild kProfilerBuild{.profiler = BE_PROFILER != 0, .tracy = BE_PROFILER_TRACY != 0};

    struct ProfilerSettings {
        ProfilerBackendKind backend = ProfilerBackendKind::None;
        eastl::string tracePath;  // not empty: the Chrome trace is written there when the application ends
    };

    /**
     * @brief Reads the profiler flags of the command line.
     *
     * | Flag | Meaning |
     * |------|---------|
     * | --trace=<file> | record with the Chrome trace backend and write <file> (relative to the working folder) at the end |
     * | --profiler=none\|chrome\|tracy | the backend; chrome without --trace records for the control method `profiler_dump` |
     *
     * Without a flag the backend is Tracy in a Tracy build and none otherwise. --trace overrides that default; together with
     * --profiler=none or --profiler=tracy it is an error. A bare or empty flag, an unknown backend, Tracy in a build without it and
     * any of the flags in a build without the profiler are errors (the start-up stops: a person who asked for a trace has to be told
     * that none is recorded). Other arguments are ignored.
     *
     * @param args Command line without the program name
     * @param build What the build contains; tests pass another one
     */
    [[nodiscard]] std::expected<ProfilerSettings, eastl::string> ResolveProfilerSettings(eastl::span<const eastl::string> args, ProfilerBuild build = kProfilerBuild);

}  // namespace BECore
