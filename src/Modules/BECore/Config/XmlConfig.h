#pragma once

#include <BECore/Config/XmlDocument.h>

namespace BECore {

    class XmlConfig {

    public:
        ~XmlConfig() = default;

        static XmlConfig Create();

        [[nodiscard]] bool LoadFromVirtualPath(StringView virtualPath) const;
        [[nodiscard]] bool LoadFromFile(const std::filesystem::path& filepath) const;
        [[nodiscard]] bool LoadFromString(StringView xmlContent) const;
        [[nodiscard]] bool SaveToFile(const std::filesystem::path& filepath) const;
        [[nodiscard]] bool IsLoaded() const;

        [[nodiscard]] XmlNode GetRoot() const;

        void Clear() const;

    private:
        explicit XmlConfig(IntrusivePtrAtomic<XmlDocument> doc) : _doc(std::move(doc)) {}

        IntrusivePtrAtomic<XmlDocument> _doc;
    };

}  // namespace BECore
