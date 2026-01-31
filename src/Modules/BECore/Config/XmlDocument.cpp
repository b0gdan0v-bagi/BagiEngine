#include "XmlDocument.h"
#include "XmlNode.h"

#include <fstream>

namespace BECore {

    bool XmlDocument::LoadFromFile(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) {
            return false;
        }

        if (!std::filesystem::is_regular_file(filepath)) {
            return false;
        }

        std::ifstream testStream(filepath, std::ios::in);
        if (!testStream.is_open()) {
            return false;
        }
        testStream.close();

        pugi::xml_parse_result result = _doc.load_file(filepath.string().c_str());
        return result;
    }

    bool XmlDocument::LoadFromString(eastl::string_view xmlContent) {
        if (xmlContent.empty()) {
            return false;
        }

        return _doc.load_string(xmlContent.data());
    }

    bool XmlDocument::SaveToFile(const std::filesystem::path& filepath) const {
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

        // Save with formatting (2 space indent)
        return _doc.save_file(filepath.string().c_str(), "  ", 
                             pugi::format_indent | pugi::format_indent_attributes, 
                             pugi::encoding_utf8);
    }

    XmlNode XmlDocument::GetRoot() const {
        if (!_doc) {
            return {};
        }
        return XmlNode{_doc.first_child()};
    }

    void XmlDocument::Clear() {
        _doc.reset();
    }

    bool XmlDocument::IsLoaded() const {
        return !_doc.empty();
    }

}  // namespace BECore
