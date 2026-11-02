#include "TextLayoutCache.h"

namespace BECore {

    namespace {

        bool SameOptions(const TextLayoutOptions& a, const TextLayoutOptions& b) {
            return a.fontSize == b.fontSize && a.maxWidth == b.maxWidth && a.maxHeight == b.maxHeight && a.align == b.align && a.lineSpacing == b.lineSpacing && a.wrap == b.wrap &&
                   a.overflow == b.overflow && a.minFontSize == b.minFontSize;
        }

    }  // namespace

    const TextLayoutResult& TextLayoutCache::Get(const Font& font, eastl::string_view utf8, const TextLayoutOptions& options) {
        const bool same = _font == &font && SameOptions(_options, options) && eastl::string_view(_text.data(), _text.size()) == utf8;
        if (!same) {
            _layout = TextLayout::Layout(font, utf8, options);
            _font = &font;
            _text.assign(utf8.data(), utf8.size());
            _options = options;
            ++_layoutCount;
        }
        return _layout;
    }

    void TextLayoutCache::Reset() {
        _font = nullptr;
        _text.clear();
        _layout = {};
    }

}  // namespace BECore
