#pragma once

#include <BECore/Utils/String.h>
#include <BECore/Reflection/TypeTraits.h>
#include <EASTL/string_view.h>

namespace BECore {

    /**
     * @brief Compile-time type identifier using hash of type name
     * 
     * ClassMeta provides a lightweight, compile-time type identification
     * mechanism using FNV-1a hash of the type name. This allows for
     * type-erased factory creation and type comparisons without RTTI.
     * 
     * @example
     * // Get ClassMeta for a reflected type
     * constexpr auto meta = GetClassMeta<ConsoleSink>();
     * 
     * // Compare types
     * if (meta == GetClassMeta<FileSink>()) { ... }
     */
    struct ClassMeta {
        uint64_t typeHash = 0;
        eastl::string_view typeName;
        
        /**
         * @brief Compare ClassMeta by hash
         */
        constexpr bool operator==(const ClassMeta& other) const {
            return typeHash == other.typeHash;
        }
        
        /**
         * @brief Compare ClassMeta by hash
         */
        constexpr bool operator!=(const ClassMeta& other) const {
            return typeHash != other.typeHash;
        }
        
        /**
         * @brief Check if ClassMeta is valid (non-zero hash)
         */
        constexpr explicit operator bool() const {
            return typeHash != 0;
        }
    };

    /**
     * @brief Get ClassMeta for a reflected type at compile-time
     * 
     * @tparam T Type with ReflectionTraits specialization
     * @return ClassMeta containing type hash and name
     * 
     * @note Requires T to have ReflectionTraits<T>::name defined
     * 
     * @example
     * constexpr auto meta = GetClassMeta<ConsoleSink>();
     * // meta.typeName == "ConsoleSink"
     * // meta.typeHash == hash of "ConsoleSink"
     */
    template<typename T>
        requires HasReflection<T>
    consteval ClassMeta GetClassMeta() {
        constexpr auto name = ReflectionTraits<T>::name;
        return ClassMeta{ String::GetHash(name), name };
    }

}  // namespace BECore
