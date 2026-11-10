#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UISprite.h>

namespace BECore {

    /**
     * @brief A filled rect, optionally with a sprite on it: a HUD plate, the back of a menu, a frame.
     *
     * Draws the box of `color` (rounded corners, border, gradient, shadow: UIBoxStyle) first, then the sprite (a TextureLibrary name) tinted with `spriteTint`. With a `sliceBorder` the sprite is cut
     * into nine pieces so that its corners keep their size and its edges stretch (UINineSlice); the border is in pixels of the sprite and is drawn one unit per pixel. A panel takes clicks
     * (`blocksInput`), so a click on it does not reach what is below; write `blocksInput="false"` for a plate that does not.
     */
    class UIPanel : public UIElement {
        BE_CLASS(UIPanel)
    public:
        UIPanel() {
            blocksInput = true;
        }
        ~UIPanel() override = default;

        BE_REFLECT_FIELD PoolString sprite;    ///< a TextureLibrary name; empty draws the fill only
        BE_REFLECT_FIELD UIEdges sliceBorder;  ///< pixels of the sprite; all zero stretches the whole sprite over the rect
        BE_REFLECT_FIELD UIColor spriteTint{255, 255, 255, 255};

        /// The sprite cache: tests and code give a texture directly with `GetSprite().SetTexture()`
        [[nodiscard]] UISprite& GetSprite() {
            return _sprite;
        }

        void DrawSelf(IRenderer& renderer) override;

    private:
        UISprite _sprite;
    };

}  // namespace BECore
