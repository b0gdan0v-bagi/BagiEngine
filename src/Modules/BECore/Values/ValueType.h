#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <type_traits>

namespace BECore {

    namespace Detail {

        /// The kinds a value may have: a number, a bool, an enum, a small object of the game or the engine (`Vec3`, `Color`, a game's `Point`)
        constexpr bool IsValueKind(const TypeDesc& type) {
            switch (type.kind) {
                case TypeKind::Bool:
                case TypeKind::Int8:
                case TypeKind::Int16:
                case TypeKind::Int32:
                case TypeKind::Int64:
                case TypeKind::UInt8:
                case TypeKind::UInt16:
                case TypeKind::UInt32:
                case TypeKind::UInt64:
                case TypeKind::Float:
                case TypeKind::Double:
                case TypeKind::Enum:
                case TypeKind::Object:
                case TypeKind::Opaque:
                    return true;
                default:
                    return false;
            }
        }

        /// A `Value` is itself an opaque, trivially copyable type, and a value of a value is no value
        template <typename T>
        concept IsValueWrapper = requires {
            typename T::ValueMarker;
        };

        template <typename T>
        concept HasValueKind = IsValueKind(DescribeType<T>()) || std::is_same_v<T, PoolString>;

    }  // namespace Detail

    /**
     * @brief The types a `Value<T>` can hold: reflected, trivially copyable, copied bit by bit out of a context object
     *
     * A number, a bool, an enum, a `PoolString` and a small object or an opaque type. A `String`, a list, a map, an optional and a pointer are
     * not (they own memory or point elsewhere, so a slot of them is not a plain read).
     */
    template <typename T>
    concept ValueType = Describable<T> && std::is_trivially_copyable_v<T> && !Detail::IsValueWrapper<T> && Detail::HasValueKind<T>;

    static_assert(ValueType<bool> && ValueType<int32_t> && ValueType<uint32_t> && ValueType<int64_t> && ValueType<float> && ValueType<double>);
    static_assert(ValueType<PoolString>);
    static_assert(ValueType<Vec2> && ValueType<IVec2> && ValueType<Vec3> && ValueType<Vec4> && ValueType<Quat> && ValueType<Rect> && ValueType<Color>);
    static_assert(!ValueType<String> && !ValueType<Vector<int32_t>> && !ValueType<int32_t*> && !ValueType<Optional<int32_t>>);

}  // namespace BECore
