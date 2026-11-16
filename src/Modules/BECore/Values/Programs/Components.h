#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>

/**
 * The components of the math types a program can take apart (`Extract`, `pos.y`) and build (`Pack`, `vec3(a, b, c)`). Every component of a type
 * has one C++ type and the components follow each other in the struct, so the byte offset of component k is `k * sizeof(storage)`: a component of a
 * field is then a field of its own at that offset, and the `static_assert`s below pin the layout the engine's structs must keep.
 */
namespace BECore {

    inline constexpr uint32_t kMaxComponents = 4;

    /// What a type with components is made of; the primary template is a type without any
    template <typename T>
    struct ComponentTraits {
        static constexpr uint32_t kCount = 0;
    };

    template <>
    struct ComponentTraits<Vec2> {
        using Storage = float;
        using Value = float;
        static constexpr uint32_t kCount = 2;
        static constexpr Array<StringView, kMaxComponents> kNames{"x", "y", "", ""};
    };

    template <>
    struct ComponentTraits<Vec3> {
        using Storage = float;
        using Value = float;
        static constexpr uint32_t kCount = 3;
        static constexpr Array<StringView, kMaxComponents> kNames{"x", "y", "z", ""};
    };

    template <>
    struct ComponentTraits<Vec4> {
        using Storage = float;
        using Value = float;
        static constexpr uint32_t kCount = 4;
        static constexpr Array<StringView, kMaxComponents> kNames{"x", "y", "z", "w"};
    };

    template <>
    struct ComponentTraits<IVec2> {
        using Storage = int32_t;
        using Value = int32_t;
        static constexpr uint32_t kCount = 2;
        static constexpr Array<StringView, kMaxComponents> kNames{"x", "y", "", ""};
    };

    template <>
    struct ComponentTraits<Quat> {
        using Storage = float;
        using Value = float;
        static constexpr uint32_t kCount = 4;
        static constexpr Array<StringView, kMaxComponents> kNames{"x", "y", "z", "w"};
    };

    /// The channels of a `Color` are bytes, but a program computes with them as `int` (0..255): there is no byte type in a program
    template <>
    struct ComponentTraits<Color> {
        using Storage = unsigned char;
        using Value = int32_t;
        static constexpr uint32_t kCount = 4;
        static constexpr Array<StringView, kMaxComponents> kNames{"r", "g", "b", "a"};
    };

    template <typename T>
    concept HasComponents = ComponentTraits<T>::kCount > 0;

#define BE_COMPONENT_AT(Type, Member, Index)                                                                                                                                                           \
    static_assert(offsetof(Type, Member) == (Index) * sizeof(ComponentTraits<Type>::Storage), "the components of " #Type " follow each other in the struct: " #Member);
    BE_COMPONENT_AT(Vec2, x, 0)
    BE_COMPONENT_AT(Vec2, y, 1)
    BE_COMPONENT_AT(Vec3, x, 0)
    BE_COMPONENT_AT(Vec3, y, 1)
    BE_COMPONENT_AT(Vec3, z, 2)
    BE_COMPONENT_AT(Vec4, x, 0)
    BE_COMPONENT_AT(Vec4, y, 1)
    BE_COMPONENT_AT(Vec4, z, 2)
    BE_COMPONENT_AT(Vec4, w, 3)
    BE_COMPONENT_AT(IVec2, x, 0)
    BE_COMPONENT_AT(IVec2, y, 1)
    BE_COMPONENT_AT(Quat, x, 0)
    BE_COMPONENT_AT(Quat, y, 1)
    BE_COMPONENT_AT(Quat, z, 2)
    BE_COMPONENT_AT(Quat, w, 3)
    BE_COMPONENT_AT(Color, r, 0)
    BE_COMPONENT_AT(Color, g, 1)
    BE_COMPONENT_AT(Color, b, 2)
    BE_COMPONENT_AT(Color, a, 3)
#undef BE_COMPONENT_AT

#define BE_COMPONENTS_FILL_SIZE(Unused, Name, Cpp)                                                                                                                                                     \
    static_assert(sizeof(Cpp) == ComponentTraits<Cpp>::kCount * sizeof(ComponentTraits<Cpp>::Storage), "a type with components is nothing but them: " #Name);
    BE_PROGRAM_TYPES_MATH(BE_COMPONENTS_FILL_SIZE, Unused)
#undef BE_COMPONENTS_FILL_SIZE

    /// The components of one program type as the checker and the lowering see them
    struct ComponentSet {
        ProgramType type = ProgramType::Float;  ///< the type of each component in a program (`int` for a `Color` channel)
        uint32_t count = 0;                     ///< 0 for a type without components
        uint32_t stride = 0;                    ///< the byte distance of two components in the value: the offset of component k is `k * stride`
        Array<StringView, kMaxComponents> names{};

        /// The index of the component named @p name
        [[nodiscard]] constexpr Optional<uint32_t> Find(StringView name) const {
            for (uint32_t i = 0; i < count; ++i) {
                if (names[i] == name) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] constexpr uint32_t Offset(uint32_t index) const {
            return index * stride;
        }
    };

    template <HasComponents T>
    [[nodiscard]] constexpr ComponentSet MakeComponentSet() {
        return ComponentSet{kProgramTypeOf<typename ComponentTraits<T>::Value>, ComponentTraits<T>::kCount, sizeof(typename ComponentTraits<T>::Storage), ComponentTraits<T>::kNames};
    }

    /// The components of @p type; `count` is 0 for a type that has none (`Rect` and the rest are not program types at all)
    [[nodiscard]] constexpr ComponentSet ComponentsOf(ProgramType type) {
        switch (type) {
#define BE_COMPONENTS_OF(Unused, Name, Cpp)                                                                                                                                                            \
    case ProgramType::Name:                                                                                                                                                                            \
        return MakeComponentSet<Cpp>();
            BE_PROGRAM_TYPES_MATH(BE_COMPONENTS_OF, Unused)
#undef BE_COMPONENTS_OF
            default:
                break;
        }
        return {};
    }

    /// The type an `Extract` or a `Pack` opcode takes apart or builds, `nullopt` for any other opcode
    [[nodiscard]] constexpr Optional<ProgramType> ComponentOpcodeType(Opcode op) {
        switch (op) {
#define BE_COMPONENT_OPCODE_TYPE(Unused, Name, Cpp)                                                                                                                                                    \
    case Opcode::Extract##Name:                                                                                                                                                                        \
    case Opcode::Pack##Name:                                                                                                                                                                           \
        return ProgramType::Name;
            BE_PROGRAM_TYPES_MATH(BE_COMPONENT_OPCODE_TYPE, Unused)
#undef BE_COMPONENT_OPCODE_TYPE
            default:
                break;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool IsPack(Opcode op) {
        switch (op) {
#define BE_COMPONENT_IS_PACK(Unused, Name, Cpp)                                                                                                                                                        \
    case Opcode::Pack##Name:                                                                                                                                                                           \
        return true;
            BE_PROGRAM_TYPES_MATH(BE_COMPONENT_IS_PACK, Unused)
#undef BE_COMPONENT_IS_PACK
            default:
                break;
        }
        return false;
    }

}  // namespace BECore
