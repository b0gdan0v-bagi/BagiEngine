#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIVariantCondition.h>

namespace BECore {

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

        /// In a file: `<set element="Hud" anchor="Top" x="0" width="90%"/>`, only the set properties are attributes
        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);

        [[nodiscard]] bool operator==(const UIElementOverride&) const = default;
    };

    /// A responsive variant: while `when` holds for the canvas bounds, `overrides` are in effect
    struct UIVariant {
        UIVariantCondition when;
        eastl::vector<UIElementOverride> overrides;

        /// In a file: `<variant when="orientation:portrait"><overrides><set element="Hud" .../></overrides></variant>`
        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);

        [[nodiscard]] bool operator==(const UIVariant&) const = default;
    };

    namespace Ser {

        /// A list of variants is `<variants><variant .../>...</variants>` in a file, not the `item` elements a general vector gets
        template <>
        struct DataAccessor<eastl::vector<UIVariant>> {
            static void Save(ISerializer& s, const eastl::vector<UIVariant>& variants, StringView key);
            static bool Load(IDeserializer& d, eastl::vector<UIVariant>& variants, StringView key);
        };

    }  // namespace Ser

    namespace PropVisit {

        /// The inspector does not edit variants yet (editor tooling is stage 7 of the UI plan): they are shown as nothing
        inline bool Visit(IPropertyVisitor& /*v*/, eastl::vector<UIVariant>& /*variants*/, StringView /*key*/) {
            return false;
        }

    }  // namespace PropVisit

    class UIVariants {
    public:
        /**
         * @brief Puts into `active` of every element of the tree under @p root the authored `style` with the matching variants applied.
         *
         * Variants apply in the order of the list, so a later one wins where two set the same property, like CSS. An override names an element;
         * one that names none is ignored. Always starts from `style`, so a shape that no variant matches any more comes back.
         * @return how many variants matched
         */
        static size_t Apply(UIElement& root, eastl::span<const UIVariant> variants, const Rect& bounds);
    };

}  // namespace BECore
