#include "XmlConfigImpl.h"

namespace BECore {

    bool XmlConfigImpl::LoadFromFile(const std::filesystem::path& filepath) {
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

        return std::visit(overload{[&filepath](pugi::xml_document& doc) {
                              pugi::xml_parse_result result = doc.load_file(filepath.string().c_str());
                              return result;
                          }},
                          _doc);
    }

    bool XmlConfigImpl::LoadFromString(eastl::string_view xmlContent) {
        if (xmlContent.empty()) {
            return {};
        }

        return std::visit(overload{[xmlContent](pugi::xml_document& doc) { return doc.load_string(xmlContent.data()); }}, _doc);
    }

    bool XmlConfigImpl::SaveToFile(const std::filesystem::path& filepath) const {
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
        return std::visit(
            overload{[&filepath](const pugi::xml_document& doc) { return doc.save_file(filepath.string().c_str(), "  ", pugi::format_indent | pugi::format_indent_attributes, pugi::encoding_utf8); }},
            _doc);
    }

    XmlNode XmlConfigImpl::GetRoot() const {
        return std::visit(overload{[](const pugi::xml_document& doc) -> XmlNode {
                              if (!doc) {
                                  return {};
                              }
                              return XmlNode{doc.first_child()};
                          }},
                          _doc);
    }

    void XmlConfigImpl::Clear() {
        std::visit(overload{[](pugi::xml_document& doc) { doc.reset(); }}, _doc);
    }

    bool XmlConfigImpl::IsLoaded() const {
        return std::visit(overload{[](const pugi::xml_document& doc) { return !doc.empty(); }}, _doc);
    }

}  // namespace BECore
