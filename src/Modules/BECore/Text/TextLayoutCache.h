#pragma once

#include <BECore/Text/TextLayout.h>

namespace BECore {

    /**
     * @brief Keeps the last TextLayoutResult of a text so that a draw which asks for the same thing again does not lay it out again.
     *
     * One cache per text (a TextComponent, a UITextComponent). The key is the font, the text and every option; any change makes a new layout on the next Get().
     */
    class TextLayoutCache {
    public:
        /// The layout of @p utf8 with @p options in @p font. The reference stays valid until the next Get() or Reset()
        [[nodiscard]] const TextLayoutResult& Get(const Font& font, eastl::string_view utf8, const TextLayoutOptions& options);

        /// Forgets the layout: call it when the font was loaded again, since a new font can sit at the address of the old one
        void Reset();

        /// How many layouts were made: a test reads it to show that an unchanged text is not laid out again
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _layoutCount;
        }

        /// The layout that the last Get() returned (empty before the first one)
        [[nodiscard]] const TextLayoutResult& GetLast() const {
            return _layout;
        }

    private:
        const Font* _font = nullptr;
        eastl::string _text;
        TextLayoutOptions _options;
        TextLayoutResult _layout;
        uint32_t _layoutCount = 0;
    };

}  // namespace BECore
