#include "XmlConfig.h"

#include <Core/FileSystem/FileSystem.h>

namespace Core {

    XmlConfig XmlConfig::Create() {
        return XmlConfig(pugi::xml_document{});
    }

    bool XmlConfig::LoadFromFile(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) {
            return {};
        }

        if (!std::filesystem::is_regular_file(filepath)) {
            return {};
        }

        return std::visit(overload{[&filepath](pugi::xml_document& doc) {
                              pugi::xml_parse_result result = doc.load_file(filepath.string().c_str());
                              return result;
                          }},
                          _doc);
    }

    bool XmlConfig::LoadFromString(std::string_view xmlContent) {
        if (xmlContent.empty()) {
            return {};
        }

        return std::visit(overload{[xmlContent](pugi::xml_document& doc) { return doc.load_string(xmlContent.data()); }}, _doc);
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

        // Сохраняем с форматированием (отступ 4 пробела)
        return std::visit(
            overload{[&filepath](const pugi::xml_document& doc) { return doc.save_file(filepath.string().c_str(), "  ", pugi::format_indent | pugi::format_indent_attributes, pugi::encoding_utf8); }},
            _doc);
    }

    XmlNode XmlConfig::GetRoot() const {
        return std::visit(overload{[](const pugi::xml_document& doc) -> XmlNode {
                              if (!doc) {
                                  return {};
                              }
                              return XmlNode{doc.first_child()};
                          }},
                          _doc);
    }

    bool XmlConfig::LoadFromVirtualPath(std::string_view virtualPath) {
        std::filesystem::path resolvedPath = FileSystem::GetInstance().ResolvePath(virtualPath);
        if (resolvedPath.empty()) {
            return {};
        }
        return LoadFromFile(resolvedPath);
    }

}  // namespace Core
