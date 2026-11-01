#include "TextSettings.h"

namespace BECore {

    namespace {

        constexpr uint32_t kMinPageSize = 64;
        constexpr uint32_t kMaxPageSize = 8192;

    }  // namespace

    TextSettings TextSettings::Default() {
        TextSettings settings;
        settings.preload = {
            {0x0020, 0x007E},  // Basic Latin
            {0x00A0, 0x00FF},  // Latin-1 Supplement
            {0x0400, 0x04FF},  // Cyrillic
            {0x2010, 0x2027},  // dashes, quotes, bullets, the ellipsis
            {0x2116, 0x2116},  // numero sign
        };
        return settings;
    }

    TextSettings TextSettings::FromConfig(const XmlNode& node) {
        TextSettings settings = Default();
        if (!node) {
            return settings;
        }
        if (auto font = node.ParseAttribute<eastl::string_view>("defaultFont")) {
            settings.defaultFont = PoolString::Intern(*font);
        }
        if (auto pageSize = node.ParseAttribute<uint32_t>("pageSize")) {
            settings.pageSize = std::clamp(*pageSize, kMinPageSize, kMaxPageSize);
        }
        if (XmlNode preload = node.GetChild("preload")) {
            settings.preload.clear();
            for (XmlNode range : preload.Children()) {
                const auto first = range.ParseAttribute<uint32_t>("from");
                const auto last = range.ParseAttribute<uint32_t>("to");
                if (!first || !last || *first > *last) {
                    LOG_ERROR("TextConfig: a preload range needs from <= to, ignoring it");
                    continue;
                }
                settings.preload.push_back({static_cast<char32_t>(*first), static_cast<char32_t>(*last)});
            }
        }
        return settings;
    }

}  // namespace BECore
