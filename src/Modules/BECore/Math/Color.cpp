#include "Color.h"

#include <BECore/Reflection/StrictText.h>

namespace BECore {

    namespace {

        constexpr size_t kChannelCount = 4;

    }  // namespace

    Optional<Color> Color::ParseColorFromString(StringView data) {
        const auto tokens = StringUtils::Split(data, ',');
        if (tokens.size() != kChannelCount) {
            return std::nullopt;
        }

        unsigned char channels[kChannelCount] = {};
        for (size_t i = 0; i < kChannelCount; ++i) {
            const auto channel = StringUtils::ParseNumber<unsigned char>(StrictText::TrimAsciiSpaces(tokens[i]));
            if (!channel) {
                return std::nullopt;
            }
            channels[i] = *channel;
        }
        return Color{channels[0], channels[1], channels[2], channels[3]};
    }

}  // namespace BECore
