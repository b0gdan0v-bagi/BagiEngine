#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UISprite.h>

namespace BECore {

    /**
     * @brief A sprite (a TextureLibrary name) drawn in the rect of the element.
     *
     * By default the picture keeps its own proportions: it is drawn as large as fits the rect and centered in it, so a sprite of any shape is never squashed
     * (for art whose cell is not square, like the Chess pieces). `stretch="true"` fills the rect instead. `color` is a fill behind the picture, `tint` multiplies the picture.
     */
    class UIImage : public UIElement {
        BE_CLASS(UIImage)
    public:
        UIImage() = default;
        ~UIImage() override = default;

        BE_REFLECT_FIELD PoolString sprite;
        BE_REFLECT_FIELD UIColor tint{255, 255, 255, 255};
        BE_REFLECT_FIELD bool stretch = false;  ///< fill the rect, whatever the proportions of the sprite

        [[nodiscard]] UISprite& GetSprite() {
            return _sprite;
        }

        void DrawSelf(IRenderer& renderer) override;

    private:
        UISprite _sprite;
    };

}  // namespace BECore
