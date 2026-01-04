#pragma once

#include <Core/Config/XmlNode.h>
#include <optional>
#include <vector>
#include <string>
#include <filesystem>
#include <variant>
#include <pugixml.hpp>

namespace Core {

    class XmlConfig {

        using RawDocument = std::variant<pugi::xml_document>;

    public:
        ~XmlConfig() = default;

        static XmlConfig Create();

        bool LoadFromVirtualPath(std::string_view virtualPath);

        bool LoadFromString(std::string_view xmlContent);

        bool SaveToFile(const std::filesystem::path& filepath) const;

        XmlNode GetRoot() const;

        void Clear() {
            std::visit(overload{[](pugi::xml_document& doc) { doc.reset(); }}, _doc);
        }

        bool IsLoaded() const {
            return std::visit(overload{[](const pugi::xml_document& doc) { return !doc.empty(); }}, _doc);
        }

    private:

        explicit XmlConfig(pugi::xml_document doc) : _doc(std::move(doc)) {}

        bool LoadFromFile(const std::filesystem::path& filepath);

        RawDocument _doc;
    };

}  // namespace Core

