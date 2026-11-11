#include "ContentVersion.h"


namespace BECore {

    namespace {

        constexpr eastl::string_view kMigrateCommand = "python3 engine/tools/migrate/migrate.py <your game folder>";

    }  // namespace

    bool IsContentRoot(eastl::string_view rootName) {
        return rootName == kSceneConfigRootName || rootName == kNodeFileRootName;
    }

    std::expected<uint32_t, eastl::string> GetContentVersion(pugi::xml_node root) {
        const pugi::xml_attribute attribute = root.attribute(kContentVersionAttribute.data());
        if (!attribute) {
            return kUnversionedContentVersion;
        }
        const char* begin = attribute.value();
        const char* end = begin + std::char_traits<char>::length(begin);
        uint32_t version = 0;
        const auto [parsedEnd, error] = std::from_chars(begin, end, version);
        if (begin == end || error != std::errc{} || parsedEnd != end) {
            return std::unexpected(Format("{} '{}' is not a whole number", kContentVersionAttribute, begin));
        }
        return version;
    }

    std::expected<void, eastl::string> CheckContentVersion(pugi::xml_node root, eastl::string_view what) {
        const std::expected<uint32_t, eastl::string> version = GetContentVersion(root);
        if (!version) {
            return std::unexpected(Format("{}: {}", what, version.error()));
        }
        if (*version < kContentVersion) {
            return std::unexpected(Format("{}: content version {}, this engine reads {}. Run: {}", what, *version, kContentVersion, kMigrateCommand));
        }
        if (*version > kContentVersion) {
            return std::unexpected(Format("{}: content version {} is newer than this engine reads ({}): update the engine", what, *version, kContentVersion));
        }
        return {};
    }

}  // namespace BECore
