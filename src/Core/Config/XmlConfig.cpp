#include "XmlConfig.h"

#include <Core/FileSystem/FileSystem.h>
#include <boost/property_tree/xml_parser.hpp>
#include <fstream>
#include <sstream>

namespace Core {

    bool XmlConfig::LoadFromFile(const std::filesystem::path& filepath) {
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

        boost::property_tree::read_xml(filepath.string(), _ptree, boost::property_tree::xml_parser::trim_whitespace);
        return true;
    }

    bool XmlConfig::LoadFromString(const std::string& xmlContent) {
        if (xmlContent.empty()) {
            return false;
        }

        std::istringstream stream(xmlContent);
        if (!stream.good()) {
            return false;
        }

        boost::property_tree::read_xml(stream, _ptree, boost::property_tree::xml_parser::trim_whitespace);
        return true;
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

        boost::property_tree::xml_writer_settings<std::string> settings(' ', 4);
        boost::property_tree::write_xml(filepath.string(), _ptree, std::locale(), settings);
        return true;
    }

    bool XmlConfig::LoadFromVirtualPath(std::string_view virtualPath) {
        std::filesystem::path resolvedPath = FileSystem::GetInstance().ResolvePath(virtualPath);
        if (resolvedPath.empty()) {
            return false;
        }
        return LoadFromFile(resolvedPath);
    }

}  // namespace Core
