#pragma once

#include <BECore/Reflection/TypeTraits.h>

namespace BECore {

    // =========================================================================
    // Reflected test data for BE_FIELD_ATTRS. The .gen.hpp is generated from this header and included in
    // ReflectionFieldAttributesTest.cpp only.
    // =========================================================================

    namespace FieldAttributesTestData {

        /// One field of each kind of attributes
        struct Annotated {
            BE_CLASS(Annotated)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("Share of the screen")) float _opacity = 0.5f;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-10, 1e3), step(0.5f)) int32_t count = 3;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, hidden, required) bool _flags = true;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("say \"hi\" \\ caf\xC3\xA9, (really)")) String _quoted;
            BE_REFLECT_FIELD int32_t plain = 7;
        };

        /// The same fields without attributes: the serialized form must be the same
        struct Unannotated {
            BE_CLASS(Unannotated)

            BE_REFLECT_FIELD float _opacity = 0.5f;
            BE_REFLECT_FIELD int32_t count = 3;
            BE_REFLECT_FIELD bool _flags = true;
            BE_REFLECT_FIELD String _quoted;
            BE_REFLECT_FIELD int32_t plain = 7;
        };

        struct Base {
            BE_CLASS(Base)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t baseValue = 1;
        };

        struct Derived : Base {
            BE_CLASS(Derived)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 5)) int32_t ownValue = 2;
        };

        /// A reflected object held by value in `Outer`: its fields are visited between the brackets of the field `inner`
        struct Inner {
            BE_CLASS(Inner)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("inner field")) int32_t innerValue = 1;
        };

        struct Outer {
            BE_CLASS(Outer)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) Inner inner;
            BE_REFLECT_FIELD int32_t after = 2;
        };

    }  // namespace FieldAttributesTestData

}  // namespace BECore
