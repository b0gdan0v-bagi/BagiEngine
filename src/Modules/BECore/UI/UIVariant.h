#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIVariantCondition.h>

namespace BECore {

    /// The properties of an element that a variant can override, named as in a file (`visible`, `anchorMin`, ...) with the first letter in capitals
    CORE_ENUM(UIOverrideProperty, uint8_t, Visible, Anchor, AnchorMin, AnchorMax, Pivot, X, Y, Width, Height, Margin, Region, Direction, FontSize)

    /// A set of UIOverrideProperty, a bit per property (`1 << property`)
    using UIOverrideMask = uint32_t;

    [[nodiscard]] constexpr UIOverrideMask OverrideBit(UIOverrideProperty property) {
        return UIOverrideMask{1} << static_cast<uint32_t>(property);
    }

    /// The properties of one named element that a variant replaces; an unset one is left as it is
    struct UIElementOverride {
        PoolString element;
        Optional<bool> visible;
        Optional<UIAnchor> anchor;
        Optional<Vec2> anchorMin;
        Optional<Vec2> anchorMax;
        Optional<Vec2> pivot;
        Optional<UILength> x;
        Optional<UILength> y;
        Optional<UILength> width;
        Optional<UILength> height;
        Optional<UIEdges> margin;     ///< stretched axes only (UIRectSpec::margin)
        Optional<PoolString> region;  ///< an empty name clears the region of the element
        Optional<UIStackDirection> direction;
        Optional<float> fontSize;

        void ApplyTo(UIElementStyle& style) const;

        /// The properties this override sets, the same ones ApplyTo() writes
        [[nodiscard]] UIOverrideMask Properties() const;

        /// In a file: `<set element="Hud" anchor="Top" x="0" width="90%"/>`, only the set properties are attributes
        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);

        [[nodiscard]] bool operator==(const UIElementOverride&) const = default;
    };

    /// A responsive variant: while `when` holds for the canvas bounds, `overrides` are in effect
    struct UIVariant {
        UIVariantCondition when;
        Vector<UIElementOverride> overrides;

        /// In a file: `<variant when="orientation:portrait"><overrides><set element="Hud" .../></overrides></variant>`
        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);

        [[nodiscard]] bool operator==(const UIVariant&) const = default;
    };

    namespace Ser {

        /// A list of variants is `<variants><variant .../>...</variants>` in a file, not the `item` elements a general vector gets
        template <>
        struct DataAccessor<Vector<UIVariant>> {
            static void Save(ISerializer& s, const Vector<UIVariant>& variants, StringView key);
            static bool Load(IDeserializer& d, Vector<UIVariant>& variants, StringView key);
        };

    }  // namespace Ser

    namespace PropVisit {

        /// The inspector does not edit variants: they are shown as nothing
        inline bool Visit(IPropertyVisitor& /*v*/, Vector<UIVariant>& /*variants*/, StringView /*key*/) {
            return false;
        }

    }  // namespace PropVisit

    /// One variant that overrides an element, as UIVariants::Sources() finds it
    struct UIVariantSource {
        size_t index = 0;               ///< the place of the variant in the list, from 0
        UIOverrideMask properties = 0;  ///< what the variant sets on the element (all its overrides that name it, together)

        [[nodiscard]] bool operator==(const UIVariantSource&) const = default;
    };

    class UIVariants {
    public:
        /**
         * @brief Puts into `active` of every element of the tree under @p root the authored `style` with the matching variants applied.
         *
         * Variants apply in the order of the list, so a later one wins where two set the same property, like CSS. An override names an element;
         * one that names none is ignored. Always starts from `style`, so a shape that no variant matches any more comes back.
         * @return how many variants matched
         */
        static size_t Apply(UIElement& root, Span<const UIVariant> variants, const Rect& bounds);

        /**
         * @brief The variants that Apply() would let override the element named @p element, in the order of the list, with the properties each sets. Empty: the style is authored.
         *
         * A variant counts when its `when` holds for @p bounds (UIVariantCondition::Matches, the test Apply() uses) and one of its overrides names @p element. It reads no tree, so it is
         * the same for every element of that name, as Apply() is. @p sources is cleared first and keeps its capacity.
         */
        static void Sources(Span<const UIVariant> variants, const Rect& bounds, PoolString element, Vector<UIVariantSource>& sources);

        /// The one-line text of Sources() for a tooltip: `authored`, or `variant 2: width, anchor` per variant (the index of the list, from 0; properties in the files' spelling), joined by `; `.
        /// @p text is cleared first and keeps its capacity.
        static void DescribeSources(Span<const UIVariantSource> sources, String& text);
    };

}  // namespace BECore
