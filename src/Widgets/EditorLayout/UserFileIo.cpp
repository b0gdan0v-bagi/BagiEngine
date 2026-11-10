#include "UserFileIo.h"

namespace BECore {

    std::expected<eastl::string, eastl::string> ReadTextFile(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            return std::unexpected(Format("'{}' is not a file", path.string()));
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::unexpected(Format("Cannot open '{}'", path.string()));
        }
        std::ostringstream content;
        content << file.rdbuf();
        if (file.bad()) {
            return std::unexpected(Format("Cannot read '{}'", path.string()));
        }
        const std::string text = content.str();
        return eastl::string(text.c_str(), text.size());
    }

    std::expected<void, eastl::string> WriteTextFileAtomically(const std::filesystem::path& path, eastl::string_view text) {
        std::error_code ec;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return std::unexpected(Format("Cannot create '{}': {}", path.parent_path().string(), ec.message()));
            }
        }

        std::filesystem::path temp = path;
        temp += std::string(kTempFileSuffix.data(), kTempFileSuffix.size());
        auto fail = [&](eastl::string detail) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return std::unexpected(std::move(detail));
        };

        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                return fail(Format("Cannot create '{}'", temp.string()));
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
            if (file.fail()) {
                return fail(Format("Cannot write '{}'", temp.string()));
            }
        }

        std::filesystem::rename(temp, path, ec);
        if (ec) {
            return fail(Format("Cannot replace '{}': {}", path.string(), ec.message()));
        }
        return {};
    }

}  // namespace BECore
