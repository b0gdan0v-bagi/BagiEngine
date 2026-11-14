#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief What `BE_FIELD_ATTRS(...)` says about one reflected field
     *
     * The generator reads the attributes from the source text and writes one constexpr `FieldAttributes` per field into
     * `ReflectionTraits<T>::fields` (`FieldInfo::attributes`). A field with no `BE_FIELD_ATTRS` has `FieldAttributes{}`.
     * The inspector (`ImGuiPropertyVisitor`) uses them; a load that has diagnostics on (`IDeserializer::SetDiagnostics`) checks `range` and `required` (`Ser::Load`).
     *
     * @note Plain data: no allocation, no dependency on a UI library. `tooltip` points at a string literal.
     */
    struct FieldAttributes {
        /// `range(min, max)` was given: `rangeMin` and `rangeMax` are valid
        bool hasRange = false;
        double rangeMin = 0.0;
        double rangeMax = 0.0;
        /// `step(s)`: the grain of an edit; 0 when the field has none
        double step = 0.0;
        /// `tooltip("...")`: text shown on hover; empty when the field has none
        StringView tooltip{};
        /// `readonly`: the inspector shows the value and does not edit it
        bool readOnly = false;
        /// `hidden`: the property visitor skips the field unless it asks for hidden ones (`IPropertyVisitor::ShowsHidden`)
        bool hidden = false;
        /// `required`: a load reports a scope that does not name the field
        bool required = false;

        constexpr bool operator==(const FieldAttributes&) const = default;
    };

}  // namespace BECore
