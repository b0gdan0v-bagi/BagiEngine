#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// Writes and reads the small UI value types as one attribute of the object being written or read (the text forms of UIText)
    namespace UIAttr {

        void WriteLength(ISerializer& s, eastl::string_view key, const UILength& length);
        /// @return false when the attribute is missing or malformed (a malformed one is reported to @p d); @p out is then left as it was
        bool ReadLength(IDeserializer& d, eastl::string_view key, UILength& out);

        void WriteEdges(ISerializer& s, eastl::string_view key, const UIEdges& edges);
        bool ReadEdges(IDeserializer& d, eastl::string_view key, UIEdges& out);

        /// "x,y" (UIText::FormatFraction2): an anchor or a pivot
        void WriteVec2Fraction(ISerializer& s, eastl::string_view key, const Vec2& value);
        /// Writes the attribute only when the optional holds a value
        void WriteOptionalVec2Fraction(ISerializer& s, eastl::string_view key, const std::optional<Vec2>& value);
        /// Sets @p out when the attribute is there and well formed; otherwise leaves it as it was (a malformed one is reported to @p d)
        void ReadOptionalVec2Fraction(IDeserializer& d, eastl::string_view key, std::optional<Vec2>& out);

        /// Writes the attribute only when the optional holds edges (even equal to the default: an override "margin is 0" must survive)
        void WriteOptionalEdges(ISerializer& s, eastl::string_view key, const std::optional<UIEdges>& edges);
        void ReadOptionalEdges(IDeserializer& d, eastl::string_view key, std::optional<UIEdges>& out);

        void WriteCorners(ISerializer& s, eastl::string_view key, const UICorners& corners);
        bool ReadCorners(IDeserializer& d, eastl::string_view key, UICorners& out);

        /// The colour of a shadow may be a theme token (`@name`): @p colorToken is its name (empty for a literal). A Read sets @p out and @p colorToken together, and leaves both as
        /// they were when the attribute is missing or malformed
        void WriteShadow(ISerializer& s, eastl::string_view key, const UIShadowSpec& shadow, PoolString colorToken);
        bool ReadShadow(IDeserializer& d, eastl::string_view key, UIShadowSpec& out, PoolString& colorToken);

        /// "width,color" (UI units), the colour a literal or a token (the token is written back as `@name`); a Read leaves its outputs as they were when the attribute is missing or
        /// malformed (a malformed one is reported to @p d)
        void WriteTextOutline(ISerializer& s, eastl::string_view key, float width, const UIColor& color);
        bool ReadTextOutline(IDeserializer& d, eastl::string_view key, float& width, UIColor& color);

        /// "offsetX,offsetY,color" (UI units), the colour a literal or a token
        void WriteTextShadow(ISerializer& s, eastl::string_view key, float offsetX, float offsetY, const UIColor& color);
        bool ReadTextShadow(IDeserializer& d, eastl::string_view key, float& offsetX, float& offsetY, UIColor& color);

        /// Writes the attribute only when the optional holds a length
        void WriteOptionalLength(ISerializer& s, eastl::string_view key, const std::optional<UILength>& length);
        /// Sets @p out when the attribute is there and well formed; otherwise leaves it as it was
        void ReadOptionalLength(IDeserializer& d, eastl::string_view key, std::optional<UILength>& out);

    }  // namespace UIAttr

}  // namespace BECore
