#pragma once

#include <BECore/Application/CommandLine.h>

namespace BECore::Stress {

    CORE_ENUM(StressScene, uint8_t, Sprites, Entities, Ui)
    /// Which time the ramp holds to its budget: `work` is the frame without the Present stage, `cpu` the whole CPU frame (CLAUDE.md, "Fixed mode")
    CORE_ENUM(StressMeasure, uint8_t, Work, Cpu)
    CORE_ENUM(StressLanguage, uint8_t, En, Ru)

    /// The largest object count the app builds (`--stress-count`, `set_count`)
    inline constexpr uint32_t kMaxCount = 1000000;
    inline constexpr uint32_t kDefaultCount = 2000;
    /// The longest run the flags may ask for (`--stress-frames`, `--stress-warmup`)
    inline constexpr uint32_t kMaxFrames = 1000000;
    inline constexpr uint32_t kDefaultFrames = 3000;
    inline constexpr uint32_t kDefaultWarmup = 120;
    inline constexpr uint32_t kMinAtlases = 1;
    inline constexpr uint32_t kMaxAtlases = 8;
    inline constexpr uint32_t kDefaultAtlases = 4;

    /// The names of the flags of the command line (the whole spelling, so a message and a test quote it as typed)
    namespace StressFlags {
        inline constexpr eastl::string_view kScene = "--stress-scene";
        inline constexpr eastl::string_view kCount = "--stress-count";
        inline constexpr eastl::string_view kFrames = "--stress-frames";
        inline constexpr eastl::string_view kWarmup = "--stress-warmup";
        inline constexpr eastl::string_view kRamp = "--stress-ramp";
        inline constexpr eastl::string_view kMeasure = "--stress-measure";
        inline constexpr eastl::string_view kInterleave = "--stress-interleave";
        inline constexpr eastl::string_view kAtlases = "--stress-atlases";
        inline constexpr eastl::string_view kLanguage = "--stress-language";
    }  // namespace StressFlags

    /// What a run is asked to do; the defaults are what a plain `BagiStress` shows
    struct StressSettings {
        StressScene scene = StressScene::Sprites;
        uint32_t count = kDefaultCount;  // 1..kMaxCount
        uint32_t frames = kDefaultFrames;
        bool framesGiven = false;  // `--stress-frames` was on the command line: a fixed run (the default `frames` alone starts none)
        uint32_t warmup = kDefaultWarmup;
        bool ramp = false;
        StressMeasure measure = StressMeasure::Work;
        bool interleave = false;  // a new atlas and blend mode on every sprite instead of runs (the worst case for a batcher)
        uint32_t atlases = kDefaultAtlases;
        StressLanguage language = StressLanguage::En;

        /// A run that measures and quits (the fixed mode or the ramp) instead of just showing the scene
        bool IsMeasuring() const {
            return ramp || framesGiven;
        }
    };

    /**
     * @brief The settings the `--stress-*` flags of @p commandLine ask for.
     *
     * | Flag | Value |
     * |------|-------|
     * | --stress-scene= | sprites, entities, ui (any case) |
     * | --stress-count= | 1..kMaxCount |
     * | --stress-frames= | 1..kMaxFrames |
     * | --stress-warmup= | 0..kMaxFrames |
     * | --stress-ramp | bare flag (the ramp starts at kRampStart: StressGame ignores --stress-count then; it wins over --stress-frames) |
     * | --stress-measure= | work, cpu |
     * | --stress-interleave | bare flag |
     * | --stress-atlases= | kMinAtlases..kMaxAtlases |
     * | --stress-language= | en, ru |
     *
     * A flag that is wrong (a bad or empty value, a value on a bare flag, no value on a valued one) is an error that names the flag;
     * the caller decides what to do with it, nothing is defaulted silently. Flags of other parts of the program are ignored.
     */
    std::expected<StressSettings, eastl::string> ParseStressSettings(const CommandLine& commandLine);

    /// The spelling of a value on the command line and in the control API: lowercase (`sprites`, `work`, `en`)
    eastl::string_view ToFlagValue(StressScene scene);
    eastl::string_view ToFlagValue(StressMeasure measure);
    eastl::string_view ToFlagValue(StressLanguage language);

    /// The value as the flag and the control API spell it (any case); nullopt for anything else
    Optional<StressScene> ParseScene(eastl::string_view text);
    Optional<StressLanguage> ParseLanguage(eastl::string_view text);

}  // namespace BECore::Stress
