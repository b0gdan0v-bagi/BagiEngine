#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UISprite.h>
#include <BECore/UI/UIStateLook.h>

namespace BECore {

    /**
     * @brief A sprite (a TextureLibrary name) in the rect of its element, as a component: `<component type="UIImageComponent" sprite="icons.play" tint="255,255,255,255"/>`.
     *
     * Like `UIImage` the picture keeps its proportions: as large as fits the rect, centred (`UISprite::FitInto`), and `stretch="true"` fills the rect instead. `tint` multiplies
     * the picture and a transparent `tint` draws nothing. The tint follows the state of the owner: `hoverTint`, `pressedTint` and `disabledTint` when they are set, else the shifts
     * of `UIStateLook::ResolveTint` (lighter under the pointer, darker pressed, half transparent disabled); a selected picture is left alone. To put an icon beside a caption, make
     * two children of a stack: this component has no geometry of its own beyond the owner's rect.
     */
    class UIImageComponent : public UIComponent {
        BE_CLASS(UIImageComponent)
    public:
        UIImageComponent() = default;
        ~UIImageComponent() override = default;

        BE_REFLECT_FIELD PoolString sprite;
        BE_REFLECT_FIELD UIColor tint{255, 255, 255, 255};
        BE_REFLECT_FIELD UIColor hoverTint;     ///< under the pointer; transparent: `tint` lightened
        BE_REFLECT_FIELD UIColor pressedTint;   ///< while pressed; transparent: `tint` darkened
        BE_REFLECT_FIELD UIColor disabledTint;  ///< when the owner is disabled; transparent: `tint` at half alpha
        BE_REFLECT_FIELD bool stretch = false;  ///< fill the rect, whatever the proportions of the sprite

        [[nodiscard]] UISprite& GetSprite() {
            return _sprite;
        }

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        /// Resolves the colour fields of this component: `tint` and the three state tints
        void ApplyTheme(UIThemes& themes) override;

        /// The tint the picture is drawn with for @p state
        [[nodiscard]] Color GetStateTint(const UIStateLook::State& state) const;

    private:
        UISprite _sprite;
    };

}  // namespace BECore
