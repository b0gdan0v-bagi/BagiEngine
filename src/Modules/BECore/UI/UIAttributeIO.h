#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// Writes and reads the small UI value types as one attribute of the object being written or read (the text forms of UIText)
    namespace UIAttr {

        void WriteLength(ISerializer& s, eastl::string_view key, const UILength& length);
        /// @return false when the attribute is missing or malformed (a malformed one is reported to @p d); @p out is then left as it was
        bool ReadLength(IDeserializer& d, eastl::string_view key, UILength& out);

        void WriteEdges(ISerializer& s, eastl::string_view key, const UIEdges& edges);
        bool ReadEdges(IDeserializer& d, eastl::string_view key, UIEdges& out);

        void WriteCorners(ISerializer& s, eastl::string_view key, const UICorners& corners);
        bool ReadCorners(IDeserializer& d, eastl::string_view key, UICorners& out);

        void WriteShadow(ISerializer& s, eastl::string_view key, const UIShadowSpec& shadow);
        bool ReadShadow(IDeserializer& d, eastl::string_view key, UIShadowSpec& out);

        /// "width,color" (UI units); a Read leaves its outputs as they were when the attribute is missing or malformed (a malformed one is reported to @p d)
        void WriteTextOutline(ISerializer& s, eastl::string_view key, float width, const Color& color);
        bool ReadTextOutline(IDeserializer& d, eastl::string_view key, float& width, Color& color);

        /// "offsetX,offsetY,color" (UI units)
        void WriteTextShadow(ISerializer& s, eastl::string_view key, float offsetX, float offsetY, const Color& color);
        bool ReadTextShadow(IDeserializer& d, eastl::string_view key, float& offsetX, float& offsetY, Color& color);

        /// Writes the attribute only when the optional holds a length
        void WriteOptionalLength(ISerializer& s, eastl::string_view key, const std::optional<UILength>& length);
        /// Sets @p out when the attribute is there and well formed; otherwise leaves it as it was
        void ReadOptionalLength(IDeserializer& d, eastl::string_view key, std::optional<UILength>& out);

    }  // namespace UIAttr

}  // namespace BECore
