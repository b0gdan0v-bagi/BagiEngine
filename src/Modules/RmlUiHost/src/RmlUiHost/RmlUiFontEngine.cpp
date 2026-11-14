#include "RmlUiFontEngine.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Text/Utf8.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>

namespace BECore {

    namespace {

        // The page of glyphs is a square this big, in pixels: smaller than the 1024 of the engine's own atlases, since every pixel size of every face has its own
        constexpr uint32_t kPageSize = 512;
        // A transparent border around each glyph, so bilinear filtering never reads the neighbour (the same as FontAtlas)
        constexpr uint32_t kGlyphPadding = 1;
        constexpr uint32_t kBytesPerPixel = 4;
        constexpr int kNormalWeight = 400;
        constexpr char32_t kEllipsis = 0x2026;
        constexpr char32_t kFirstPrintable = 0x20;
        constexpr char32_t kDelete = 0x7F;
        constexpr char32_t kFirstLatin1Printable = 0xA0;
        // The first and the last control characters above ASCII (C1), which have no glyph to draw
        constexpr char32_t kFirstC1 = 0x80;
        constexpr float kXHeightFallbackPerSize = 0.5f;
        constexpr float kUnderlineOffsetPerSize = 0.1f;
        constexpr float kUnderlineThicknessPerSize = 1.0f / 16.0f;
        constexpr float kMinUnderlineThickness = 1.0f;
        constexpr size_t kQuadVertices = 4;

        Rml::String ToLower(Rml::String text) {
            for (char& c : text) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return text;
        }

        int ToWeight(Rml::Style::FontWeight weight) {
            return weight == Rml::Style::FontWeight::Auto ? kNormalWeight : static_cast<int>(weight);
        }

        /// A control character has no glyph and no advance; RmlUi turns whitespace into spaces before it asks for text
        bool HasNoGlyph(char32_t codePoint) {
            return codePoint < kFirstPrintable || codePoint == kDelete || (codePoint >= kFirstC1 && codePoint < kFirstLatin1Printable);
        }

        /// CSS `font-kerning: auto` turns kerning off when letters are spaced out
        bool IsKerningEnabled(const Rml::TextShapingContext& context) {
            switch (context.font_kerning) {
                case Rml::Style::FontKerning::None:
                    return false;
                case Rml::Style::FontKerning::Normal:
                    return true;
                case Rml::Style::FontKerning::Auto:
                    break;
            }
            return context.letter_spacing == 0.0f;
        }

        float RoundToPixel(float value) {
            return std::floor(value + 0.5f);
        }

    }  // namespace

    RmlUiFontEngine::RmlUiFontEngine() = default;
    RmlUiFontEngine::~RmlUiFontEngine() = default;

    size_t RmlUiFontEngine::GetPageCount() const {
        size_t pages = 0;
        for (const auto& handle : _handles) {
            pages += handle->pages.size();
        }
        return pages;
    }

    bool RmlUiFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace, Rml::Style::FontWeight weight) {
        // The family is the one the font says it has (what RmlUi's own engine reads from FreeType)
        return LoadFontFace(fileName, faceIndex, Rml::String(), Rml::Style::FontStyle::Normal, weight, fallbackFace);
    }

    bool RmlUiFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) {
        if (faceIndex != 0) {
            LOG_ERROR("RmlUiFontEngine: '{}': only the first face of a font file is read"_format(fileName.c_str()));
            return false;
        }
        if (fallbackFace) {
            LOG_WARNING("RmlUiFontEngine: '{}': fallback faces are not supported, it is loaded as an ordinary face"_format(fileName.c_str()));
        }
        auto handle = CoreManager::GetResourceManager().Load<Font>(FromStd(fileName));
        if (!handle || handle->GetState() != ResourceState::Loaded) {
            LOG_ERROR("RmlUiFontEngine: cannot load the font '{}'"_format(fileName.c_str()));
            return false;
        }
        return AddFace(family, style, weight, handle.GetPtr());
    }

    bool RmlUiFontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool /*fallbackFace*/) {
        if (faceIndex != 0) {
            LOG_ERROR("RmlUiFontEngine: only the first face of a font is read");
            return false;
        }
        auto font = Font::CreateFromMemory("<rmlui>"_intern, eastl::vector<uint8_t>(data.begin(), data.end()));
        if (font->GetState() != ResourceState::Loaded) {
            LOG_ERROR("RmlUiFontEngine: the font data cannot be read");
            return false;
        }
        return AddFace(family, style, weight, font);
    }

    bool RmlUiFontEngine::AddFace(Rml::String family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, IntrusivePtr<Font> font) {
        if (family.empty()) {
            const eastl::string name = font->GetFamilyName();
            family.assign(name.data(), name.size());
        }
        if (family.empty()) {
            LOG_ERROR("RmlUiFontEngine: the font has no family name, so a document cannot name it; pass one to LoadFontFace");
            return false;
        }
        auto face = eastl::make_unique<Face>();
        face->family = ToLower(std::move(family));
        face->style = style;
        face->weight = ToWeight(weight);
        face->font = std::move(font);
        // A handle keeps a pointer to its face: the list of faces holds them in place
        _faces.push_back(std::move(face));
        return true;
    }

    const RmlUiFontEngine::Face* RmlUiFontEngine::FindFace(const Rml::String& family, Rml::Style::FontStyle style, int weight) const {
        const Rml::String wanted = ToLower(family);
        const Face* best = nullptr;
        int bestScore = 0;
        for (const auto& face : _faces) {
            if (face->family != wanted) {
                continue;
            }
            // Another style is worse than any difference in weight
            constexpr int kStyleMismatch = 1000;
            const int score = (face->style == style ? 0 : kStyleMismatch) + std::abs(face->weight - weight);
            if (best == nullptr || score < bestScore) {
                best = face.get();
                bestScore = score;
            }
        }
        return best;
    }

    Rml::FontMetrics RmlUiFontEngine::MakeMetrics(const Font& font, int size) {
        const FontVerticalMetrics vertical = font.GetVerticalMetrics(static_cast<float>(size));
        Rml::FontMetrics metrics{};
        metrics.size = size;
        metrics.ascent = vertical.ascent;
        metrics.descent = -vertical.descent;  // RmlUi counts it positive below the baseline
        metrics.line_spacing = vertical.LineHeight();
        const uint32_t xGlyph = font.FindGlyph(U'x');
        metrics.x_height = xGlyph != 0 ? static_cast<float>(font.RasterizeGlyph(xGlyph, static_cast<float>(size)).height) : static_cast<float>(size) * kXHeightFallbackPerSize;
        metrics.underline_position = std::max(1.0f, static_cast<float>(size) * kUnderlineOffsetPerSize);
        metrics.underline_thickness = std::max(kMinUnderlineThickness, static_cast<float>(size) * kUnderlineThicknessPerSize);
        metrics.has_ellipsis = font.FindGlyph(kEllipsis) != 0;
        return metrics;
    }

    Rml::FontFaceHandle RmlUiFontEngine::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) {
        if (size <= 0) {
            return 0;
        }
        const Face* face = FindFace(family, style, ToWeight(weight));
        if (face == nullptr) {
            return 0;
        }
        for (const auto& handle : _handles) {
            if (handle->face == face && handle->size == size) {
                return reinterpret_cast<Rml::FontFaceHandle>(handle.get());
            }
        }
        auto handle = eastl::make_unique<Handle>();
        handle->face = face;
        handle->size = size;
        handle->metrics = MakeMetrics(*face->font, size);
        Handle* raw = handle.get();
        _handles.push_back(std::move(handle));
        return reinterpret_cast<Rml::FontFaceHandle>(raw);
    }

    Rml::FontEffectsHandle RmlUiFontEngine::PrepareFontEffects(Rml::FontFaceHandle /*handle*/, const Rml::FontEffectList& fontEffects) {
        if (!fontEffects.empty() && !_warnedAboutEffects) {
            _warnedAboutEffects = true;
            LOG_WARNING("RmlUiFontEngine: font effects (outline, shadow, blur, glow) are not supported, so the text is drawn without them");
        }
        return 0;
    }

    const Rml::FontMetrics& RmlUiFontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
        static const Rml::FontMetrics kEmpty{};
        const auto* face = reinterpret_cast<const Handle*>(handle);
        return face != nullptr ? face->metrics : kEmpty;
    }

    int RmlUiFontEngine::GetVersion(Rml::FontFaceHandle /*handle*/) {
        // A page is never rewritten, so geometry made from it never goes stale
        return 0;
    }

    int RmlUiFontEngine::GetStringWidth(Rml::FontFaceHandle faceHandle, Rml::StringView string, const Rml::TextShapingContext& textShapingContext, Rml::Character priorCharacter) {
        auto* handle = reinterpret_cast<Handle*>(faceHandle);
        if (handle == nullptr) {
            return 0;
        }
        const Font& font = *handle->face->font;
        const float size = static_cast<float>(handle->size);
        const bool kerning = IsKerningEnabled(textShapingContext);

        uint32_t previous = priorCharacter != Rml::Character::Null ? font.ResolveGlyph(static_cast<char32_t>(priorCharacter)) : 0;
        float width = 0.0f;
        const StringView text(string.begin(), string.size());
        size_t position = 0;
        while (position < text.size()) {
            const char32_t codePoint = Utf8::DecodeNext(text, position);
            const Glyph* glyph = GetOrAppendGlyph(*handle, codePoint);
            if (glyph == nullptr) {
                continue;
            }
            if (kerning && previous != 0) {
                width += font.GetKerning(previous, glyph->index, size);
            }
            width += glyph->advance + textShapingContext.letter_spacing;
            previous = glyph->index;
        }
        return std::max(0, static_cast<int>(RoundToPixel(width)));
    }

    int RmlUiFontEngine::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle, Rml::FontEffectsHandle /*fontEffectsHandle*/, Rml::StringView string, Rml::Vector2f position,
                                        Rml::ColourbPremultiplied colour, float /*opacity*/, const Rml::TextShapingContext& textShapingContext, Rml::TexturedMeshList& meshList) {
        auto* handle = reinterpret_cast<Handle*>(faceHandle);
        if (handle == nullptr) {
            return 0;
        }
        const Font& font = *handle->face->font;
        const float size = static_cast<float>(handle->size);
        const bool kerning = IsKerningEnabled(textShapingContext);

        // One mesh per page that the text touches, in the order the pages first appear
        eastl::hash_map<int, size_t> meshOfPage;
        meshList.clear();

        uint32_t previous = 0;
        float pen = 0.0f;
        const StringView text(string.begin(), string.size());
        size_t offset = 0;
        while (offset < text.size()) {
            const char32_t codePoint = Utf8::DecodeNext(text, offset);
            const Glyph* glyph = GetOrAppendGlyph(*handle, codePoint);
            if (glyph == nullptr) {
                continue;
            }
            if (kerning && previous != 0) {
                pen += font.GetKerning(previous, glyph->index, size);
            }
            if (glyph->page >= 0) {
                auto found = meshOfPage.find(glyph->page);
                if (found == meshOfPage.end()) {
                    found = meshOfPage.emplace(glyph->page, meshList.size()).first;
                    Rml::TexturedMesh& textured = meshList.emplace_back();
                    textured.texture = GetPage(*handle, glyph->page)->source.GetTexture(renderManager);
                }
                Rml::Mesh& mesh = meshList[found->second].mesh;
                // Each glyph lands on a whole pixel, so the page is drawn 1:1 and stays sharp
                const float left = RoundToPixel(position.x + pen + glyph->offsetX);
                const float top = RoundToPixel(position.y + glyph->offsetY);
                const float right = left + glyph->width;
                const float bottom = top + glyph->height;
                const int base = static_cast<int>(mesh.vertices.size());
                mesh.vertices.reserve(mesh.vertices.size() + kQuadVertices);
                mesh.vertices.push_back({{left, top}, colour, {glyph->u0, glyph->v0}});
                mesh.vertices.push_back({{right, top}, colour, {glyph->u1, glyph->v0}});
                mesh.vertices.push_back({{left, bottom}, colour, {glyph->u0, glyph->v1}});
                mesh.vertices.push_back({{right, bottom}, colour, {glyph->u1, glyph->v1}});
                for (const int corner : {0, 1, 2, 2, 1, 3}) {
                    mesh.indices.push_back(base + corner);
                }
            }
            pen += glyph->advance + textShapingContext.letter_spacing;
            previous = glyph->index;
        }
        return std::max(0, static_cast<int>(RoundToPixel(pen)));
    }

    RmlUiFontEngine::Page* RmlUiFontEngine::GetPage(Handle& handle, int index) const {
        return handle.pages[static_cast<size_t>(index)].get();
    }

    int RmlUiFontEngine::AppendToPage(Handle& handle, uint32_t width, uint32_t height, ShelfPacker::Slot& slot) {
        // The last page takes the glyph unless a texture was made from it already
        if (!handle.pages.empty() && !handle.pages.back()->sealed) {
            if (const auto place = handle.pages.back()->packer.Pack(width, height)) {
                slot = *place;
                return static_cast<int>(handle.pages.size()) - 1;
            }
        }
        if (width > kPageSize || height > kPageSize) {
            return -1;
        }
        auto page = eastl::make_unique<Page>(kPageSize);
        Page* raw = page.get();
        // The texture is made when RmlUi first draws with it, for each of its contexts; the pixels are final from then on
        raw->source = Rml::CallbackTextureSource([raw](const Rml::CallbackTextureInterface& textureInterface) {
            raw->sealed = true;
            return textureInterface.GenerateTexture(Rml::Span<const Rml::byte>(raw->pixels.data(), raw->pixels.size()), Rml::Vector2i(static_cast<int>(raw->size), static_cast<int>(raw->size)));
        });
        const auto place = raw->packer.Pack(width, height);
        handle.pages.push_back(std::move(page));
        if (!place) {
            return -1;
        }
        slot = *place;
        return static_cast<int>(handle.pages.size()) - 1;
    }

    const RmlUiFontEngine::Glyph* RmlUiFontEngine::GetOrAppendGlyph(Handle& handle, char32_t codePoint) {
        if (HasNoGlyph(codePoint)) {
            return nullptr;
        }
        const auto found = handle.glyphs.find(codePoint);
        if (found != handle.glyphs.end()) {
            return &found->second;
        }

        const Font& font = *handle.face->font;
        const float size = static_cast<float>(handle.size);
        Glyph glyph;
        glyph.index = font.ResolveGlyph(codePoint);
        glyph.advance = font.GetAdvance(glyph.index, size);

        const GlyphBitmap bitmap = font.RasterizeGlyph(glyph.index, size);
        if (bitmap.width > 0 && bitmap.height > 0) {
            ShelfPacker::Slot slot;
            const int pageIndex = AppendToPage(handle, bitmap.width + 2 * kGlyphPadding, bitmap.height + 2 * kGlyphPadding, slot);
            if (pageIndex < 0) {
                LOG_WARNING("RmlUiFontEngine: the glyph of U+{:04X} at {} px does not fit a page, so it is not drawn"_format(static_cast<uint32_t>(codePoint), handle.size));
            } else {
                Page& page = *handle.pages[static_cast<size_t>(pageIndex)];
                // White, with the coverage in alpha; premultiplied, so the color channels hold the coverage too
                for (uint32_t row = 0; row < bitmap.height; ++row) {
                    for (uint32_t column = 0; column < bitmap.width; ++column) {
                        const uint8_t coverage = bitmap.coverage[static_cast<size_t>(row) * bitmap.width + column];
                        const size_t target = (static_cast<size_t>(slot.y + kGlyphPadding + row) * page.size + slot.x + kGlyphPadding + column) * kBytesPerPixel;
                        page.pixels[target] = coverage;
                        page.pixels[target + 1] = coverage;
                        page.pixels[target + 2] = coverage;
                        page.pixels[target + 3] = coverage;
                    }
                }
                const float pageSize = static_cast<float>(page.size);
                glyph.page = pageIndex;
                glyph.u0 = static_cast<float>(slot.x + kGlyphPadding) / pageSize;
                glyph.v0 = static_cast<float>(slot.y + kGlyphPadding) / pageSize;
                glyph.u1 = static_cast<float>(slot.x + kGlyphPadding + bitmap.width) / pageSize;
                glyph.v1 = static_cast<float>(slot.y + kGlyphPadding + bitmap.height) / pageSize;
                glyph.offsetX = static_cast<float>(bitmap.offsetX);
                glyph.offsetY = static_cast<float>(bitmap.offsetY);
                glyph.width = static_cast<float>(bitmap.width);
                glyph.height = static_cast<float>(bitmap.height);
            }
        }
        return &handle.glyphs.emplace(codePoint, glyph).first->second;
    }

    void RmlUiFontEngine::ReleaseFontResources() {
        // Every handle is invalid after this; the faces stay
        _handles.clear();
    }

    void RmlUiFontEngine::Shutdown() {
        _handles.clear();
        _faces.clear();
        _warnedAboutEffects = false;
    }

}  // namespace BECore
