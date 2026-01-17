#pragma once

/**
 * @file ReflectionMarkers.h
 * @brief Macros for marking classes, fields, and enums for reflection
 *
 * These macros are used by the reflection parser (reflector.py) to identify
 * types that need reflection metadata generation. The generated code provides
 * ReflectionTraits<T> specializations and EnumUtils compatibility.
 *
 * @example
 * struct BE_REFLECT_CLASS Player {
 *     BE_REFLECT_FIELD int32_t health = 100;
 *     BE_REFLECT_FIELD float speed = 5.0f;
 *     BE_REFLECT_FIELD PoolString name;
 * };
 *
 * enum class BE_REFLECT_ENUM Direction : uint8_t {
 *     North, South, East, West
 * };
 */

namespace BECore {

// Clang/GCC annotation macros for reflection parser
#if defined(__clang__) || defined(__GNUC__)
    /**
     * @brief Mark a class/struct for reflection
     *
     * The reflection parser will generate ReflectionTraits<T> specialization
     * for this type, enabling serialization via the SaveSystem.
     */
    #define BE_REFLECT_CLASS [[clang::annotate("reflect_class")]]

    /**
     * @brief Mark a field for reflection
     *
     * Only fields marked with this macro will be included in the generated
     * ReflectionTraits<T>::fields tuple.
     */
    #define BE_REFLECT_FIELD [[clang::annotate("reflect_field")]]

    /**
     * @brief Mark an enum for reflection
     *
     * The reflection parser will generate EnumTokenizerPtr compatible with
     * EnumUtils<T>, providing ToString/FromString functionality.
     */
    #define BE_REFLECT_ENUM [[clang::annotate("reflect_enum")]]
#else
    // MSVC: use comment markers as fallback since MSVC lacks [[annotate]]
    // The reflection parser detects these via regex patterns
    #define BE_REFLECT_CLASS /* BE_REFLECT_CLASS */
    #define BE_REFLECT_FIELD /* BE_REFLECT_FIELD */
    #define BE_REFLECT_ENUM  /* BE_REFLECT_ENUM */
#endif

}  // namespace BECore
