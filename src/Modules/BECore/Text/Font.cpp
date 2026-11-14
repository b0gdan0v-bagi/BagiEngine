#include "Font.h"

#include <BECore/Text/Utf8.h>
#include <Generated/Font.gen.hpp>

// The one translation unit that holds the stb_truetype implementation (src/Modules/stb/README.md). The functions stay
// internal to it, so they cannot clash with the copy inside ImGui.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
#include <stb_truetype.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace BECore {

    namespace {

        constexpr char32_t kQuestionMark = U'?';
        constexpr char32_t kReplacementCharacter = 0xFFFD;
        // The 'name' table id of the font family ("PT Sans"), and the size of one UTF-16 unit in it
        constexpr int kFamilyNameId = 1;
        constexpr int kUtf16UnitBytes = 2;
        constexpr int kByteBits = 8;

    }  // namespace

    struct Font::Impl {
        stbtt_fontinfo info{};
    };

    Font::Font() = default;
    Font::~Font() = default;

    IntrusivePtr<Font> Font::CreateFromMemory(PoolString name, Vector<uint8_t> data) {
        auto font = New<Font>();
        if (data.empty()) {
            font->SetFailed(name);
            return font;
        }
        font->_data = std::move(data);
        font->_impl = MakeUnique<Impl>();
        const unsigned char* bytes = font->_data.data();
        const int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
        if (offset < 0 || stbtt_InitFont(&font->_impl->info, bytes, offset) == 0) {
            font->SetFailed(name);
            return font;
        }
        font->_path = name;
        font->_state = ResourceState::Loaded;
        return font;
    }

    void Font::SetFailed(PoolString name) {
        _path = name;
        _data.clear();
        _impl.reset();
        _state = ResourceState::Failed;
    }

    ResourceState Font::GetState() const {
        return _state;
    }

    PoolString Font::GetPath() const {
        return _path;
    }

    String Font::GetFamilyName() const {
        String family;
        if (!_impl) {
            return family;
        }
        int length = 0;
        // The Windows entry is UTF-16 big endian (only the Basic Multilingual Plane is read: a family name outside it would be a surprise)
        const char* name = stbtt_GetFontNameString(&_impl->info, &length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, kFamilyNameId);
        if (name != nullptr && length >= kUtf16UnitBytes) {
            for (int i = 0; i + 1 < length; i += kUtf16UnitBytes) {
                const auto high = static_cast<char32_t>(static_cast<unsigned char>(name[i]));
                const auto low = static_cast<char32_t>(static_cast<unsigned char>(name[i + 1]));
                Utf8::Append(family, (high << kByteBits) | low);
            }
            return family;
        }
        // The Macintosh entry is one byte per character
        name = stbtt_GetFontNameString(&_impl->info, &length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, STBTT_MAC_LANG_ENGLISH, kFamilyNameId);
        if (name != nullptr) {
            family.assign(name, static_cast<size_t>(length));
        }
        return family;
    }

    uint64_t Font::GetMemoryUsage() const {
        return _data.size();
    }

    PoolString Font::GetTypeName() const {
        return "Font"_intern;
    }

    uint32_t Font::FindGlyph(char32_t codePoint) const {
        if (!_impl) {
            return 0;
        }
        return static_cast<uint32_t>(stbtt_FindGlyphIndex(&_impl->info, static_cast<int>(codePoint)));
    }

    uint32_t Font::ResolveGlyph(char32_t codePoint) const {
        if (uint32_t glyph = FindGlyph(codePoint); glyph != 0) {
            return glyph;
        }
        if (uint32_t glyph = FindGlyph(kReplacementCharacter); glyph != 0) {
            return glyph;
        }
        return FindGlyph(kQuestionMark);
    }

    FontVerticalMetrics Font::GetVerticalMetrics(float fontSize) const {
        if (!_impl) {
            return {};
        }
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&_impl->info, &ascent, &descent, &lineGap);
        const float scale = stbtt_ScaleForMappingEmToPixels(&_impl->info, fontSize);
        return {static_cast<float>(ascent) * scale, static_cast<float>(descent) * scale, static_cast<float>(lineGap) * scale};
    }

    float Font::GetAdvance(uint32_t glyph, float fontSize) const {
        if (!_impl) {
            return 0.0f;
        }
        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetGlyphHMetrics(&_impl->info, static_cast<int>(glyph), &advance, &leftSideBearing);
        return static_cast<float>(advance) * stbtt_ScaleForMappingEmToPixels(&_impl->info, fontSize);
    }

    float Font::GetKerning(uint32_t left, uint32_t right, float fontSize) const {
        if (!_impl) {
            return 0.0f;
        }
        const int kerning = stbtt_GetGlyphKernAdvance(&_impl->info, static_cast<int>(left), static_cast<int>(right));
        return static_cast<float>(kerning) * stbtt_ScaleForMappingEmToPixels(&_impl->info, fontSize);
    }

    GlyphBitmap Font::RasterizeGlyph(uint32_t glyph, float fontSize) const {
        GlyphBitmap bitmap;
        if (!_impl) {
            return bitmap;
        }
        const float scale = stbtt_ScaleForMappingEmToPixels(&_impl->info, fontSize);
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&_impl->info, static_cast<int>(glyph), scale, scale, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0) {
            return bitmap;
        }
        bitmap.width = static_cast<uint32_t>(x1 - x0);
        bitmap.height = static_cast<uint32_t>(y1 - y0);
        bitmap.offsetX = x0;
        bitmap.offsetY = y0;
        bitmap.coverage.resize(static_cast<size_t>(bitmap.width) * bitmap.height, 0);
        stbtt_MakeGlyphBitmap(&_impl->info, bitmap.coverage.data(), static_cast<int>(bitmap.width), static_cast<int>(bitmap.height), static_cast<int>(bitmap.width), scale, scale,
                              static_cast<int>(glyph));
        return bitmap;
    }

}  // namespace BECore
