#pragma once

namespace BECore {

    class IRenderer;
    class UIElement;
    class UIThemes;
    struct UIContentFit;

    /**
     * @brief One behaviour or look of a UIElement: an image, a text, a click. An element is the set of its components, `UIElement::components`.
     *
     * `<element type="UIElement" name="Play" ...><components><component type="UIInputClicker" action="Play"/></components></element>`. The type is the class name in
     * AbstractFactory<UIComponent>. An element holds **one component of each type** (a second of a type in a file is logged and dropped).
     *
     * A component keeps no pointer to its element: every virtual takes the owner, so there is no cycle and nothing dangles. The owner's order is the box, then the components
     * in list order (DrawSelf), then the children; the pointer hooks run in list order after the element's own. All the virtuals have empty defaults, so a component
     * overrides what it uses. The pure rules a component applies (the state looks) are not here: they live next to the element (UIStateLook).
     */
    class UIComponent : public RefCounted {
        BE_CLASS(UIComponent, FACTORY_BASE, ELEMENT = component)
    public:
        UIComponent() = default;
        ~UIComponent() override = default;

        /// Draws in the rect of @p owner, after the owner's own box and before its children, with the view transform of the canvas in effect (canvas units)
        virtual void DrawSelf(IRenderer& renderer, UIElement& owner);

        /// What the component says about its content in a rect of @p width x @p height canvas units at @p scale output pixels per unit; the validator combines it with the owner's
        [[nodiscard]] virtual UIContentFit CheckContent(const UIElement& owner, float width, float height, float scale) const;

        /// The pointer hooks of the owner (`UIElement::OnPress`, `OnDrag`, `OnClick`), forwarded in list order; @p x and @p y in canvas units
        virtual void OnPress(UIElement& owner, float x, float y);
        virtual void OnDrag(UIElement& owner, float x, float y);
        virtual void OnClick(UIElement& owner);

        /// Writes the current theme's colours into the colour fields of this component that are tokens (`UIColor::token`); called by `UIElement::ApplyTheme` when the theme version moved.
        /// A component with no colour keeps the empty default
        virtual void ApplyTheme(UIThemes& themes);

        /// The component takes clicks, so the owner is `UIElement::IsInteractive()`: hit-testable, and the validator checks its touch size
        [[nodiscard]] virtual bool MakesInteractive() const;

        /// The component shows the pointer state (hover, press, disabled, selected) in the owner's own box, which then draws with the state look. A panel that only blocks input says
        /// no: it is hovered too and must not light up
        [[nodiscard]] virtual bool GivesStateLook() const;

        /// The component says its owner is *on* (a toggle's variable holds its on value): `UIElement::GetVisualState` puts it in `UIStateLook::State::checked` of an element that owns its state,
        /// and the children of that element are drawn with it (the switch of a toggle row). Nothing else reads it
        [[nodiscard]] virtual bool IsChecked(const UIElement& owner) const;
    };

}  // namespace BECore
