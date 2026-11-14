#pragma once

#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/ShelfPacker.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>

namespace BECore {

    /**
     * @brief RmlUi's font engine over the engine's own `Font` (stb_truetype), so the library needs no FreeType.
     *
     * A face is a `Font` registered under a family, a style and a weight. A handle (what `GetFontFaceHandle` returns) is a face at one pixel size: it keeps the
     * metrics, the glyphs drawn so far and the pages they sit on. A page is an RGBA picture (white, coverage in alpha, premultiplied as RmlUi wants) that
     * becomes a texture for each RmlUi context on first use, through a callback texture; the page is then sealed, and a glyph that comes later goes to a new
     * page, so the pixels a texture was made from never change. Text is laid out with fractional advances and kerning (the `kern` table, as `Font` reads it)
     * and each glyph is put on a whole pixel, as `TextRenderer` does; the widths RmlUi asks for are whole pixels.
     *
     * Not done: font effects (outline, shadow, blur), fallback faces, synthesized bold or italic, shaping. A document that asks for an effect gets none, and the
     * engine says so once. Main thread only.
     */
    class RmlUiFontEngine final : public Rml::FontEngineInterface {
    public:
        RmlUiFontEngine();
        ~RmlUiFontEngine() override;

        bool LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace, Rml::Style::FontWeight weight) override;
        bool LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) override;
        bool LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) override;

        Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) override;
        Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle, const Rml::FontEffectList& fontEffects) override;
        const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
        int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& textShapingContext, Rml::Character priorCharacter) override;
        int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle, Rml::FontEffectsHandle fontEffectsHandle, Rml::StringView string, Rml::Vector2f position,
                           Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& textShapingContext, Rml::TexturedMeshList& meshList) override;
        int GetVersion(Rml::FontFaceHandle handle) override;
        void ReleaseFontResources() override;
        void Shutdown() override;

        /// How many faces are registered, and how many pages the handles hold (a test and the log read them)
        [[nodiscard]] size_t GetFaceCount() const {
            return _faces.size();
        }
        [[nodiscard]] size_t GetPageCount() const;

    private:
        struct Face {
            Rml::String family;  // lower case: a CSS family name is matched without regard to case
            Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
            int weight = 0;
            IntrusivePtr<Font> font;
        };

        struct Glyph {
            int page = -1;  // -1: no ink (a space)
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float advance = 0.0f;
            uint32_t index = 0;  // in the font
        };

        struct Page {
            Page(uint32_t size) : packer(size, size), size(size), pixels(static_cast<size_t>(size) * size * 4, 0) {}

            ShelfPacker packer;
            uint32_t size = 0;
            Vector<uint8_t> pixels;
            // Set when a texture was made from the pixels: from then on the page takes no more glyphs
            bool sealed = false;
            Rml::CallbackTextureSource source;
        };

        struct Handle {
            const Face* face = nullptr;
            int size = 0;
            Rml::FontMetrics metrics{};
            eastl::hash_map<char32_t, Glyph> glyphs;
            Vector<UniquePtr<Page>> pages;
        };

        bool AddFace(Rml::String family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, IntrusivePtr<Font> font);
        const Face* FindFace(const Rml::String& family, Rml::Style::FontStyle style, int weight) const;
        static Rml::FontMetrics MakeMetrics(const Font& font, int size);

        /// The glyph of @p codePoint at this handle's size, drawn onto a page on first use; null for a code point that needs no glyph (a control character)
        const Glyph* GetOrAppendGlyph(Handle& handle, char32_t codePoint);
        int AppendToPage(Handle& handle, uint32_t width, uint32_t height, ShelfPacker::Slot& slot);
        Page* GetPage(Handle& handle, int index) const;

        Vector<UniquePtr<Face>> _faces;
        Vector<UniquePtr<Handle>> _handles;
        bool _warnedAboutEffects = false;
    };

}  // namespace BECore
