#pragma once

#include <BECore/Application/CommandLine.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

namespace BECore::Stress {

    /// `RmlUi` exists only in a build with the RmlUi host (STRESS_WITH_RMLUI, set by tools/stress/CMakeLists.txt); an `#if` cannot sit inside the macro's arguments
#ifdef STRESS_WITH_RMLUI
    CORE_ENUM(StressScene, uint8_t, Sprites, Entities, Ui, RmlUi)
#else
    CORE_ENUM(StressScene, uint8_t, Sprites, Entities, Ui)
#endif
    /// Which time the ramp holds to its budget: `work` is the frame without the Present stage, `cpu` the whole CPU frame (CLAUDE.md, "Fixed mode")
    CORE_ENUM(StressMeasure, uint8_t, Work, Cpu)
    CORE_ENUM(StressLanguage, uint8_t, En, Ru)

    /// The largest object count the app builds (`--stress-count`, `set_count`)
    inline constexpr uint32_t kMaxCount = 4000000;
    inline constexpr uint32_t kDefaultCount = 2000;
    /// The longest run the flags may ask for (`--stress-frames`, `--stress-warmup`)
    inline constexpr uint32_t kMaxFrames = 1000000;
    inline constexpr uint32_t kDefaultFrames = 3000;
    inline constexpr uint32_t kDefaultWarmup = 120;
    inline constexpr uint32_t kMinAtlases = 1;
    inline constexpr uint32_t kMaxAtlases = 8;
    inline constexpr uint32_t kDefaultAtlases = 4;
    /// The frame time a step of the ramp must keep unless the flag says otherwise: a frame that fits 60 fps (1000 / 60, rounded down)
    inline constexpr double kDefaultRampBudgetMs = 16.6;
    inline constexpr double kMinRampBudgetMs = 1.0;
    inline constexpr double kMaxRampBudgetMs = 1000.0;
    /// The largest side `--stress-sprite-size` takes: the height of the view, so a sprite of any allowed side fits the field
    inline constexpr uint32_t kMaxSpriteSize = 1080;

    /// The names of the flags of the command line (the whole spelling, so a message and a test quote it as typed)
    namespace StressFlags {
        inline constexpr StringView kScene = "--stress-scene";
        inline constexpr StringView kCount = "--stress-count";
        inline constexpr StringView kFrames = "--stress-frames";
        inline constexpr StringView kWarmup = "--stress-warmup";
        inline constexpr StringView kRamp = "--stress-ramp";
        inline constexpr StringView kMeasure = "--stress-measure";
        inline constexpr StringView kBudgetMs = "--stress-budget-ms";
        inline constexpr StringView kInterleave = "--stress-interleave";
        inline constexpr StringView kNoBatching = "--stress-no-batching";
        inline constexpr StringView kAtlases = "--stress-atlases";
        inline constexpr StringView kLanguage = "--stress-language";
        inline constexpr StringView kSpriteSize = "--stress-sprite-size";
    }  // namespace StressFlags

    /// What loads the `sprites` scene beyond its plain field; the loads combine, the defaults are the plain field, and the line of a run carries each
    struct StressLoad {
        uint32_t spriteSize = 0;  // the side of every sprite (1..kMaxSpriteSize); 0: each has its own, from the spread of SpriteField
    };

    /// What a run is asked to do; the defaults are what a plain `BagiStress` shows
    struct StressSettings {
        StressScene scene = StressScene::Sprites;
        uint32_t count = kDefaultCount;  // 1..kMaxCount
        uint32_t frames = kDefaultFrames;
        bool framesGiven = false;  // `--stress-frames` was on the command line: a fixed run (the default `frames` alone starts none)
        uint32_t warmup = kDefaultWarmup;
        bool ramp = false;
        StressMeasure measure = StressMeasure::Work;
        double budgetMs = kDefaultRampBudgetMs;  // the frame time the ramp holds its measure to
        bool interleave = false;                 // a new atlas and blend mode on every sprite instead of runs (the worst case for a batcher)
        bool noBatching = false;                 // the renderer's 2D batcher is turned off at the first update: the comparison of a GPU backend with and without it
        uint32_t atlases = kDefaultAtlases;
        StressLanguage language = StressLanguage::En;
        StressLoad load;

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
     * | --stress-scene= | sprites, entities, ui, rmlui only with STRESS_WITH_RMLUI (any case) |
     * | --stress-count= | 1..kMaxCount |
     * | --stress-frames= | 1..kMaxFrames |
     * | --stress-warmup= | 0..kMaxFrames |
     * | --stress-ramp | bare flag (the ramp starts at kRampStart: StressGame ignores --stress-count then; it wins over --stress-frames) |
     * | --stress-measure= | work, cpu |
     * | --stress-budget-ms= | a decimal from kMinRampBudgetMs to kMaxRampBudgetMs |
     * | --stress-interleave | bare flag |
     * | --stress-no-batching | bare flag |
     * | --stress-atlases= | kMinAtlases..kMaxAtlases |
     * | --stress-language= | en, ru |
     * | --stress-sprite-size= | 1..kMaxSpriteSize |
     *
     * A flag that is wrong (a bad or empty value, a value on a bare flag, no value on a valued one) is an error that names the flag;
     * the caller decides what to do with it, nothing is defaulted silently. Flags of other parts of the program are ignored.
     */
    std::expected<StressSettings, String> ParseStressSettings(const CommandLine& commandLine);

    /// The spelling of a value on the command line and in the control API: lowercase (`sprites`, `work`, `en`)
    StringView ToFlagValue(StressScene scene);
    StringView ToFlagValue(StressMeasure measure);
    StringView ToFlagValue(StressLanguage language);

    /// The value as the flag and the control API spell it (any case); nullopt for anything else
    Optional<StressScene> ParseScene(StringView text);
    Optional<StressLanguage> ParseLanguage(StringView text);

}  // namespace BECore::Stress
