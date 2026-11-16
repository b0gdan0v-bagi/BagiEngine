#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>

namespace BECore {

    /**
     * @brief Why an access by name failed; shared by the path parser, `GetProperty` / `SetProperty` and the compiled paths
     *
     * The names are the error codes of the control protocol (`ToString` gives the wire text), so they stay snake case.
     */
    CORE_ENUM(PropertyErrorCode, uint8_t, invalid_path, too_long, not_found, not_a_value, type_mismatch, out_of_range, read_only, hidden, interning, no_visitor, not_fixed)

    namespace PropertyPathLimits {

        /// Bytes of a whole path
        inline constexpr size_t kMaxPathBytes = 512;

        /// Segments of a path (`a.b[2].c` has four)
        inline constexpr size_t kMaxPathSegments = 32;

        /// Bytes of a text key after its escapes are resolved
        inline constexpr size_t kMaxKeyBytes = 128;

        /// The largest index of `[n]`: the position of a sequence element or an integer map key
        inline constexpr uint32_t kMaxIndex = std::numeric_limits<uint32_t>::max();

    }  // namespace PropertyPathLimits

    CORE_ENUM(PathSegmentKind, uint8_t, Field, Index, Key)

    /// One step of a path: the serialized name of a field, the position of a sequence element or an integer key, or the text key of a map
    struct PathSegment {
        PathSegmentKind kind = PathSegmentKind::Field;
        /// The field name or the key (escapes resolved); empty for an index
        String text;
        uint32_t index = 0;
    };

    struct PropertyPath {
        Vector<PathSegment> segments;
    };

    /// A refused path: the code, the byte of the text it was found at, and a message that says what was expected there
    struct PathError {
        PropertyErrorCode code = PropertyErrorCode::invalid_path;
        size_t position = 0;
        String message;
    };

    /**
     * @brief Reads `name`, `a.b`, `a[2]`, `a["text key"]`, `a.b[2].c`; a path starts with a name
     *
     * A name is `[A-Za-z_][A-Za-z0-9_]*` (the serialized name of a field), an index is decimal digits up to `kMaxIndex`, a key is
     * quoted with `\"` and `\\` as its only escapes. Client text: nothing is interned and no input asserts.
     */
    [[nodiscard]] std::expected<PropertyPath, PathError> ParsePropertyPath(StringView text);

    /// The text of the first @p count segments, as `ParsePropertyPath` reads it back; for messages
    [[nodiscard]] String FormatPropertyPath(Span<const PathSegment> segments, size_t count);

}  // namespace BECore
