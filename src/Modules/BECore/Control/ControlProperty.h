#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <expected>

namespace BECore {

    /// What the methods that read and write a field by name share: the `value` on the wire and the codes of a refused access

    /// The member `value` of @p params as the value of a field: a boolean, an integer (`int64_t`, or `uint64_t` above its range; past 64 bits it is a number),
    /// a finite number or a string; `invalid_params` for an absent or any other value
    [[nodiscard]] std::expected<PropertyValue, ControlError> ReadPropertyValue(JsonView params);

    /// @p value as the writer writes it: the bare scalar
    void WritePropertyValue(JsonWriter& writer, const PropertyValue& value);

    /// The wire code of a refused access. A path over a limit is an invalid path to a client
    [[nodiscard]] ControlError MakePropertyControlError(const PropertyError& error);

}  // namespace BECore
