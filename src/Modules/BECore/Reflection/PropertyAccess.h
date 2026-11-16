#pragma once

#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /**
     * @brief The value of a leaf field as a client sees it
     *
     * A read gives `bool` for a bool, `int64_t` for a signed integer, `uint64_t` for an unsigned one, `double` for a `float` or a
     * `double`, and `String` for `String`, `FixedString`, `PoolString` and for an enum (its name).
     */
    using PropertyValue = Variant<bool, int64_t, uint64_t, double, String>;

    /// A refused access: the code (the error codes of the control protocol) and a message that names the path and the reason
    struct PropertyError {
        PropertyErrorCode code = PropertyErrorCode::not_found;
        String message;
    };

    struct SetOptions {
        /**
         * @brief Whether a `PoolString` field may be written
         *
         * Writing one interns the text, and the pool never frees: a client's text must not grow it unchecked, so the default
         * refuses with `interning`. Whoever turns it on owns the limit on the text.
         */
        bool allowInterning = false;
    };

    /**
     * @brief Reads the leaf at @p path of the object @p root
     *
     * A field is named by its serialized name (`opacity` for `_opacity`), a field of a parent like an own field, a sequence element
     * by `[i]`, a map value by its key (`["text"]`, or `[3]` for an integer key), a pointer, a pair (`first`, `second`) and an
     * object by their members, an `Optional` by its value (`maybe`; an empty one is `not_found`), a `Variant` by `choice.value`
     * (the active alternative). A path ends at a leaf (a number, a text, an enum, a bool): a struct, a list or a map is
     * `not_a_value`. A `hidden` field can be read. Never changes @p root, never interns.
     */
    [[nodiscard]] std::expected<PropertyValue, PropertyError> GetProperty(TypedRef root, StringView path);
    [[nodiscard]] std::expected<PropertyValue, PropertyError> GetProperty(TypedRef root, const PropertyPath& path);

    /**
     * @brief Writes the leaf at @p path; a refused write changes nothing
     *
     * Value rules: a bool field takes a `bool`; an integer field takes an `int64_t` or `uint64_t` that fits its type (`out_of_range`
     * otherwise, a `double` is `type_mismatch` even when whole); a `float` or `double` field takes any number kind but never a
     * non-finite one, and a `double` beyond the `float` range is `out_of_range`; a text field takes a `String`, and a `FixedString`
     * that would cut it refuses (`out_of_range`) and keeps its old value; a `PoolString` is `interning` unless
     * `options.allowInterning`; an enum field takes a `String` that is one of its names or an integer index below the count. The
     * `range` attribute of the innermost open field is checked on a numeric write, also for the numeric element of a container
     * field. A const root, a `readonly` field and a `hidden` field anywhere on the path refuse (`read_only`, `hidden`). It adds
     * and removes no element of a container and engages no `Optional`.
     */
    [[nodiscard]] std::expected<void, PropertyError> SetProperty(TypedRef root, StringView path, const PropertyValue& value, const SetOptions& options = {});
    [[nodiscard]] std::expected<void, PropertyError> SetProperty(TypedRef root, const PropertyPath& path, const PropertyValue& value, const SetOptions& options = {});

}  // namespace BECore
