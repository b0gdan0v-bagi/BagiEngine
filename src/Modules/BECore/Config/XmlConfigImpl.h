#pragma once

namespace BECore {

    class XmlNode;

    class XmlConfigImpl : public RefCountedNonAtomic {
    public:
        using RawDocument = std::variant<pugi::xml_document>;

        explicit XmlConfigImpl(pugi::xml_document doc) : _doc(std::move(doc)) {}

        bool LoadFromFile(const std::filesystem::path& filepath);

        bool LoadFromString(eastl::string_view xmlContent);

        bool SaveToFile(const std::filesystem::path& filepath) const;

        XmlNode GetRoot() const;

        void Clear();

        bool IsLoaded() const;

    private:
        RawDocument _doc;
    };

}  // namespace BECore

