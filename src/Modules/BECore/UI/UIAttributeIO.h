#pragma once

#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// Writes and reads the small UI value types as one attribute of the object being written or read (the text forms of UIText)
    namespace UIAttr {

        void WriteLength(ISerializer& s, eastl::string_view key, const UILength& length);
        /// @return false when the attribute is missing or malformed (a malformed one is reported to @p d); @p out is then left as it was
        bool ReadLength(IDeserializer& d, eastl::string_view key, UILength& out);

        void WriteEdges(ISerializer& s, eastl::string_view key, const UIEdges& edges);
        bool ReadEdges(IDeserializer& d, eastl::string_view key, UIEdges& out);

        /// Writes the attribute only when the optional holds a length
        void WriteOptionalLength(ISerializer& s, eastl::string_view key, const std::optional<UILength>& length);
        /// Sets @p out when the attribute is there and well formed; otherwise leaves it as it was
        void ReadOptionalLength(IDeserializer& d, eastl::string_view key, std::optional<UILength>& out);

    }  // namespace UIAttr

}  // namespace BECore
