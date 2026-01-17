#pragma once

#include <BECore/Reflection/IArchive.h>
#include <BECore/Reflection/TypeTraits.h>
#include <EASTL/vector.h>
#include <type_traits>

namespace BECore {

    // =============================================================================
    // Type traits for serialization
    // =============================================================================

    /**
     * @brief Check if a type is a primitive that IArchive can serialize directly
     */
    template <typename T>
    concept ArchivePrimitive = 
        std::is_same_v<T, bool> ||
        std::is_same_v<T, int8_t> ||
        std::is_same_v<T, uint8_t> ||
        std::is_same_v<T, int16_t> ||
        std::is_same_v<T, uint16_t> ||
        std::is_same_v<T, int32_t> ||
        std::is_same_v<T, uint32_t> ||
        std::is_same_v<T, int64_t> ||
        std::is_same_v<T, uint64_t> ||
        std::is_same_v<T, float> ||
        std::is_same_v<T, double> ||
        std::is_same_v<T, eastl::string> ||
        std::is_same_v<T, PoolString>;

    /**
     * @brief Check if a type is a vector
     */
    template <typename T>
    struct IsVector : std::false_type {};

    template <typename T, typename Alloc>
    struct IsVector<eastl::vector<T, Alloc>> : std::true_type {};

    template <typename T>
    concept VectorType = IsVector<std::remove_cv_t<T>>::value;

    // =============================================================================
    // Forward declarations
    // =============================================================================

    template <typename T>
    void Serialize(IArchive& archive, eastl::string_view name, T& value);

    // =============================================================================
    // Primitive serialization
    // =============================================================================

    /**
     * @brief Serialize a primitive type
     */
    template <ArchivePrimitive T>
    void Serialize(IArchive& archive, eastl::string_view name, T& value) {
        archive.Serialize(name, value);
    }

    // =============================================================================
    // Enum serialization
    // =============================================================================

    /**
     * @brief Serialize an enum using EnumUtils
     *
     * Stores enums as strings for human-readable archives,
     * or as underlying type for binary archives.
     */
    template <typename T>
        requires std::is_enum_v<T>
    void Serialize(IArchive& archive, eastl::string_view name, T& value) {
        if (archive.IsWriting()) {
            // Write as string using EnumUtils
            auto str = eastl::string(EnumUtils<T>::ToString(value).data(),
                                     EnumUtils<T>::ToString(value).size());
            archive.Serialize(name, str);
        } else {
            // Read as string and convert back
            eastl::string str;
            archive.Serialize(name, str);
            auto result = EnumUtils<T>::Cast(eastl::string_view{str.data(), str.size()});
            if (result.has_value()) {
                value = result.value();
            }
        }
    }

    // =============================================================================
    // Reflected class serialization
    // =============================================================================

    /**
     * @brief Serialize a reflected class using its ReflectionTraits
     *
     * Iterates over all reflected fields and serializes each one.
     */
    template <typename T>
        requires HasReflection<T>
    void Serialize(IArchive& archive, eastl::string_view name, T& value) {
        if (archive.BeginObject(name)) {
            std::apply([&](auto&&... fields) {
                (Serialize(archive, fields.name, value.*(fields.ptr)), ...);
            }, ReflectionTraits<T>::fields);
            archive.EndObject();
        }
    }

    // =============================================================================
    // Vector serialization
    // =============================================================================

    /**
     * @brief Serialize a vector of elements
     */
    template <VectorType T>
    void Serialize(IArchive& archive, eastl::string_view name, T& value) {
        size_t count = value.size();
        
        if (archive.BeginArray(name, count)) {
            if (archive.IsReading()) {
                value.resize(count);
            }
            
            for (size_t i = 0; i < count; ++i) {
                // Use index as element name
                char indexName[16];
                snprintf(indexName, sizeof(indexName), "%zu", i);
                Serialize(archive, eastl::string_view{indexName}, value[i]);
            }
            
            archive.EndArray();
        }
    }

    // =============================================================================
    // Root object serialization helpers
    // =============================================================================

    /**
     * @brief Serialize an object as the root element
     *
     * Convenience function for serializing the top-level object.
     */
    template <typename T>
        requires HasReflection<T>
    void SerializeRoot(IArchive& archive, T& value) {
        Serialize(archive, ReflectionTraits<T>::name, value);
    }

    /**
     * @brief Serialize an object with a custom root name
     */
    template <typename T>
        requires HasReflection<T>
    void SerializeRoot(IArchive& archive, eastl::string_view rootName, T& value) {
        Serialize(archive, rootName, value);
    }

}  // namespace BECore
