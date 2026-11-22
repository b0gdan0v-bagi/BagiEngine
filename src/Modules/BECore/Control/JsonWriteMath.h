#pragma once

#include <BECore/Control/JsonWriter.h>

/**
 * @file JsonWriteMath.h
 * @brief The `Math` value types written into a JsonWriter as the object `Serialize` makes: one number per component, keys sorted.
 *
 * `SaveField` (JsonWriteReflect.h) calls these for a field of such a type and for the elements of a sequence of them (all have at least two
 * components, so an element never collapses to a scalar). The types are only declared here: this header is included by every generated file, the
 * definitions include the types. `Mat4` has no `Serialize` (a transform is built, never stored) and no writer.
 *
 * A new `Math` type with a `Serialize` needs its overload here, or a reflected field of it takes the DOM bridge.
 */

namespace BECore {

    struct Color;
    struct IVec2;
    struct Quat;
    struct Rect;
    struct Vec2;
    struct Vec3;
    struct Vec4;

}  // namespace BECore

namespace BECore::JsonWrite {

    void WriteMath(JsonWriter& w, const Vec2& value);
    void WriteMath(JsonWriter& w, const Vec3& value);
    void WriteMath(JsonWriter& w, const Vec4& value);
    void WriteMath(JsonWriter& w, const IVec2& value);
    void WriteMath(JsonWriter& w, const Quat& value);
    void WriteMath(JsonWriter& w, const Color& value);
    void WriteMath(JsonWriter& w, const Rect& value);

    /// The type has a `WriteMath` overload
    template <typename T>
    concept HasWriteMath = requires(JsonWriter & writer, const T& value) {
        WriteMath(writer, value);
    };

}  // namespace BECore::JsonWrite
