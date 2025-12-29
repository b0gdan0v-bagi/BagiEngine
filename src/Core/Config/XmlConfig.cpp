#include "XmlConfig.h"

#include <Core/FileSystem/FileSystem.h>
#include <algorithm>
#include <sstream>

namespace Core {

    bool XmlConfig::LoadFromFile(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) {
            return {};
        }

        if (!std::filesystem::is_regular_file(filepath)) {
            return {};
        }

        std::ifstream testStream(filepath, std::ios::in);
        if (!testStream.is_open()) {
            return {};
        }
        testStream.close();

        pugi::xml_parse_result result = _doc.load_file(filepath.string().c_str());
        return result;
    }

    bool XmlConfig::LoadFromString(const std::string& xmlContent) {
        if (xmlContent.empty()) {
            return {};
        }

        bool result = _doc.load_string(xmlContent.c_str());
        return result;
    }

    bool XmlConfig::SaveToFile(const std::filesystem::path& filepath) const {
        auto parentPath = filepath.parent_path();
        if (!parentPath.empty()) {
            if (!std::filesystem::exists(parentPath)) {
                std::error_code ec;
                if (!std::filesystem::create_directories(parentPath, ec)) {
                    return false;
                }
            }
        }

        std::ofstream testStream(filepath, std::ios::out);
        if (!testStream.is_open()) {
            return false;
        }
        testStream.close();

        // Сохраняем с форматированием (отступ 4 пробела)
        return _doc.save_file(filepath.string().c_str(), "  ", pugi::format_indent | pugi::format_indent_attributes, pugi::encoding_utf8);
    }

    XmlNode XmlConfig::GetRoot() const {
        if (!_doc) {
            return {};
        }
        return XmlNode{_doc.first_child()};
    }

    bool XmlConfig::LoadFromVirtualPath(std::string_view virtualPath) {
        std::filesystem::path resolvedPath = FileSystem::GetInstance().ResolvePath(virtualPath);
        if (resolvedPath.empty()) {
            return {};
        }
        return LoadFromFile(resolvedPath);
    }

}  // namespace Core
