#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UISprite.h>
#include <BECore/UI/UIStateLook.h>

namespace BECore {

    /**
     * @brief A sprite (a TextureLibrary name) in the rect of its element, as a component: `<component type="UIImageComponent" sprite="icons.play" tint="255,255,255,255"/>`.
     *
     * The picture keeps its proportions: as large as fits the rect, centred (`UISprite::FitInto`), and `stretch="true"` fills the rect instead. `tint` multiplies
     * the picture and a transparent `tint` draws nothing. The tint follows the state of the owner: `hoverTint`, `pressedTint` and `disabledTint` when they are set, else the shifts
     * of `UIStateLook::ResolveTint` (lighter under the pointer, darker pressed, half transparent disabled); a selected picture is left alone. The state is the owner's, or, when the owner has no
     * `UIInputClicker`, that of the nearest ancestor that has one (`UIElement::GetVisualState`): the picture in a child of a button follows the button. To put an icon beside a caption, make two
     * children of a stack: this component has no geometry of its own beyond the owner's rect.
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

        /// The icon of a button, as a share of the shorter side of the rect when the element has no size of its own (the numbers the HTML compiler writes for an `img` beside a caption)
        static constexpr float kIconOnlyShare = 0.6f;      ///< a button with no caption
        static constexpr float kIconWithTextShare = 0.5f;  ///< a button with a caption: of its height
        static constexpr float kIconPadding = 8.0f;        ///< the air between the edge of the rect and an icon next to a caption, in canvas units
        static constexpr float kIconGap = 6.0f;            ///< and between that icon and the caption

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
