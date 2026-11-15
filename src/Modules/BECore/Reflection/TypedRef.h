#pragma once

#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <concepts>
#include <type_traits>

namespace BECore {

    namespace Detail {

        template <typename T>
        concept HasVirtualTypeMeta = requires(const T& object) {
            {object.GetTypeMeta()}->std::convertible_to<const ClassMeta&>;
        };

        template <typename T>
        concept HasStaticTypeMeta = requires {
            {T::GetStaticTypeMeta()}->std::convertible_to<const ClassMeta&>;
        };

    }  // namespace Detail

    /**
     * @brief An erased pointer to a live reflected object and the `TypeInfo` of its **most derived** type
     *
     * The handle every access by name takes (`GetProperty`, `CallMethod`, ...). `object` is the address of the object of `type`
     * itself, never of a base subobject, so the registry's offsets apply to it as they are. It does not own the object and it is
     * not kept past the call that made it. `isConst` is a promise the callee keeps (a const object is only read, a const method
     * only called): `Address()` does not check it.
     */
    struct TypedRef {
        void* object = nullptr;
        const TypeInfo* type = nullptr;
        bool isConst = false;

        explicit operator bool() const {
            return object != nullptr && type != nullptr;
        }

        [[nodiscard]] void* Address() const {
            return object;
        }

        [[nodiscard]] const void* ConstAddress() const {
            return object;
        }

        /**
         * @brief The handle of a reflected @p object, whatever type the reference has
         *
         * The most derived type comes from the virtual `GetTypeMeta()` (a type without one, a `BE_EVENT`, is its static type) and
         * is found in @p registry by hash; the address is moved back from the `T` subobject to the derived object by the chain's
         * offsets. Empty when the derived type is not registered, or `T` is not on its chain.
         */
        template <typename T>
        requires Detail::HasStaticTypeMeta<std::remove_const_t<T>> [[nodiscard]] static TypedRef Of(T& object, const TypeRegistry& registry = TypeRegistry::GetInstance()) {
            using Plain = std::remove_const_t<T>;
            const TypeInfo* declared = registry.Find(Plain::GetStaticTypeMeta().typeHash);
            if (declared == nullptr) {
                return {};
            }
            const TypeInfo* derived = declared;
            if constexpr (Detail::HasVirtualTypeMeta<Plain>) {
                derived = registry.Find(object.GetTypeMeta().typeHash);
            }
            if (derived == nullptr) {
                return {};
            }
            const Optional<size_t> offset = registry.OffsetOfBase(*derived, *declared);
            if (!offset.has_value()) {
                return {};
            }
            // The derived object starts `offset` bytes before its `T` subobject: char arithmetic on the address of a live object
            auto* address = const_cast<char*>(reinterpret_cast<const char*>(std::addressof(object))) - *offset;
            return TypedRef{address, derived, std::is_const_v<T>};
        }

        /// The subobject of the reflected ancestor @p ancestor (the type itself counts); empty when it is not one
        [[nodiscard]] TypedRef Base(const TypeInfo& ancestor, const TypeRegistry& registry = TypeRegistry::GetInstance()) const {
            if (!*this) {
                return {};
            }
            const Optional<size_t> offset = registry.OffsetOfBase(*type, ancestor);
            if (!offset.has_value()) {
                return {};
            }
            return TypedRef{static_cast<char*>(object) + *offset, &ancestor, isConst};
        }
    };

}  // namespace BECore
