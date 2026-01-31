#include "Color.h"

namespace BECore {

    std::optional<Color> Color::ParseColorFromString(eastl::string_view data) {
        if (data.empty()) {
            return {};
        }

        const auto& tokens = BECore::String::Split(data, ',');

        if (tokens.size() != 4) {
            return {};
        }

        const auto fromStringView = [](eastl::string_view s) -> unsigned char {
            unsigned char result;
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
            if (ec == std::errc{}) {
                return result;
            }
            return 0;
        };

        return Color{fromStringView(tokens[0]), fromStringView(tokens[1]), fromStringView(tokens[2]), fromStringView(tokens[3])};
    }

}  // namespace BECore

