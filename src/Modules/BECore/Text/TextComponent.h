#pragma once

#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextLayoutCache.h>

namespace BECore {

    /**
     * @brief Draws text in the box of the node's TransformComponent (x, y, width, height).
     *
     * A width of 0 means no limit: no wrap, no cut on the width; the same for the height. The font size is in scene
     * units: the text is rasterized at that size times the scale of the view transform (a scene with a view size that
     * is fitted to a bigger output gets sharper text, not stretched text). Usable outside the UI: it is an ordinary scene component.
     *
     * Overflow: Clip clips the drawing to the box when the text does not fit, Ellipsis ends the text with "…", AutoFit
     * shrinks the font down to `_minFontSize` (TextLayout).
     */
    class TextComponent : public IComponent {
        BE_CLASS(TextComponent)
    public:
        TextComponent() = default;
        ~TextComponent() override = default;

        BE_REFLECT_FIELD eastl::string _text;
        BE_REFLECT_FIELD PoolString _fontPath;        ///< a font path; empty: the face of `_fontFamily` and `_fontWeight`. A path wins over them
        BE_REFLECT_FIELD PoolString _fontFamily;      ///< a family of the `families` table of TextConfig.xml; empty is its default family
        BE_REFLECT_FIELD uint16_t _fontWeight = 400;  ///< the CSS weight (400 regular, 700 bold); the nearest face of the family is used
        BE_REFLECT_FIELD float _fontSize = 24.0f;
        BE_REFLECT_FIELD Color _color{255, 255, 255, 255};
        BE_REFLECT_FIELD TextAlign _align = TextAlign::Left;
        BE_REFLECT_FIELD TextOverflow _overflow = TextOverflow::Clip;
        BE_REFLECT_FIELD bool _wrap = true;
        BE_REFLECT_FIELD float _lineSpacing = 1.0f;
        BE_REFLECT_FIELD float _minFontSize = 8.0f;

        void OnAttached() override;
        void OnDraw() override;

        void SetText(eastl::string_view text);

        /// Load the font again from `_fontPath`, or `_fontFamily` and `_fontWeight`. A changed path, family or weight is noticed by the next draw by itself;
        /// call it to read the file again
        void ReloadFont();

        /// The layout that the last draw used (empty before the first draw)
        [[nodiscard]] const TextLayoutResult& GetLastLayout() const {
            return _layoutCache.GetLast();
        }

    private:
        ResourceHandle<Font> _font;
        bool _fontTried = false;
        FontRequest _fontRequest;  // what the loaded font (or the failed try) was asked for
        TextLayoutCache _layoutCache;
    };

}  // namespace BECore
