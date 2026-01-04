#pragma once

#include <Core/Config/XmlNode.h>
#include <optional>
#include <vector>
#include <string>
#include <filesystem>

namespace Core {

    class XmlConfig {
    public:
        XmlConfig() = default;
        ~XmlConfig() = default;

        bool LoadFromFile(const std::filesystem::path& filepath);

        bool LoadFromVirtualPath(std::string_view virtualPath);

        bool LoadFromString(const std::string& xmlContent);

        bool SaveToFile(const std::filesystem::path& filepath) const;

        XmlNode GetRoot() const;

        void Clear() {
            _doc.reset();
        }

        bool IsLoaded() const {
            return !_doc.empty();
        }

    private:

        pugi::xml_document _doc;
    };

}  // namespace Core

