#include "StressSettings.h"

namespace BECore::Stress {

    namespace {

        constexpr eastl::string_view kSceneNames[] = {"sprites", "entities", "ui"};
        constexpr eastl::string_view kLanguageNames[] = {"en", "ru"};
        constexpr eastl::string_view kMeasureNames[] = {"work", "cpu"};

        bool EqualsIgnoreCase(eastl::string_view a, eastl::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        /// The index of @p text in @p names (any case), or nullopt
        template <size_t N>
        Optional<size_t> IndexOf(const eastl::string_view (&names)[N], eastl::string_view text) {
            for (size_t i = 0; i < N; ++i) {
                if (EqualsIgnoreCase(names[i], text)) {
                    return i;
                }
            }
            return std::nullopt;
        }

        /// The whole of @p text as a number; nullopt for an empty text, a sign, a trailing character or a number that does not fit
        Optional<uint32_t> ParseNumber(eastl::string_view text) {
            uint32_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return value;
        }

        eastl::string FlagError(eastl::string_view flag, eastl::string_view what) {
            return Format("{}: {}", flag, what);
        }

        /// Reads the number of a valued flag into @p target when the flag is there; an error when it is wrong
        Optional<eastl::string> ReadNumber(const CommandLine& line, eastl::string_view flag, uint32_t minimum, uint32_t maximum, uint32_t& target) {
            const auto value = line.FindValue(flag);
            if (!value) {
                return line.HasFlag(flag) ? Optional(FlagError(flag, "needs a value")) : std::nullopt;
            }
            const auto number = ParseNumber(*value);
            if (!number || *number < minimum || *number > maximum) {
                return FlagError(flag, Format("'{}' is not a whole number from {} to {}", *value, minimum, maximum));
            }
            target = *number;
            return std::nullopt;
        }

        /// The error of a bare flag that was given a value (`--stress-ramp=1`), or nullopt
        Optional<eastl::string> CheckBareFlag(const CommandLine& line, eastl::string_view flag) {
            return line.FindValue(flag) ? Optional(FlagError(flag, "is a flag without a value")) : std::nullopt;
        }

        /// Reads the choice of a valued flag into @p target when the flag is there; an error when it is wrong
        template <typename E, size_t N>
        Optional<eastl::string> ReadChoice(const CommandLine& line, eastl::string_view flag, const eastl::string_view (&names)[N], E& target) {
            const auto value = line.FindValue(flag);
            if (!value) {
                return line.HasFlag(flag) ? Optional(FlagError(flag, "needs a value")) : std::nullopt;
            }
            const auto index = IndexOf(names, *value);
            if (!index) {
                eastl::string choices;
                for (size_t i = 0; i < N; ++i) {
                    choices += (i == 0 ? "" : ", ");
                    choices.append(names[i].data(), names[i].size());
                }
                return FlagError(flag, Format("'{}' is not one of: {}", *value, choices));
            }
            target = static_cast<E>(*index);
            return std::nullopt;
        }

    }  // namespace

    std::expected<StressSettings, eastl::string> ParseStressSettings(const CommandLine& commandLine) {
        StressSettings settings;

        const Optional<eastl::string> errors[] = {
            ReadChoice(commandLine, StressFlags::kScene, kSceneNames, settings.scene),
            ReadNumber(commandLine, StressFlags::kCount, 1, kMaxCount, settings.count),
            ReadNumber(commandLine, StressFlags::kFrames, 1, kMaxFrames, settings.frames),
            ReadNumber(commandLine, StressFlags::kWarmup, 0, kMaxFrames, settings.warmup),
            CheckBareFlag(commandLine, StressFlags::kRamp),
            ReadChoice(commandLine, StressFlags::kMeasure, kMeasureNames, settings.measure),
            CheckBareFlag(commandLine, StressFlags::kInterleave),
            ReadNumber(commandLine, StressFlags::kAtlases, kMinAtlases, kMaxAtlases, settings.atlases),
            ReadChoice(commandLine, StressFlags::kLanguage, kLanguageNames, settings.language),
        };
        for (const auto& error : errors) {
            if (error) {
                return std::unexpected(*error);
            }
        }

        settings.framesGiven = commandLine.FindValue(StressFlags::kFrames).has_value();
        settings.ramp = commandLine.HasFlag(StressFlags::kRamp);
        settings.interleave = commandLine.HasFlag(StressFlags::kInterleave);
        return settings;
    }

    eastl::string_view ToFlagValue(StressScene scene) {
        return kSceneNames[static_cast<size_t>(scene)];
    }

    eastl::string_view ToFlagValue(StressMeasure measure) {
        return kMeasureNames[static_cast<size_t>(measure)];
    }

    eastl::string_view ToFlagValue(StressLanguage language) {
        return kLanguageNames[static_cast<size_t>(language)];
    }

    Optional<StressScene> ParseScene(eastl::string_view text) {
        const auto index = IndexOf(kSceneNames, text);
        return index ? Optional(static_cast<StressScene>(*index)) : std::nullopt;
    }

    Optional<StressLanguage> ParseLanguage(eastl::string_view text) {
        const auto index = IndexOf(kLanguageNames, text);
        return index ? Optional(static_cast<StressLanguage>(*index)) : std::nullopt;
    }

}  // namespace BECore::Stress
