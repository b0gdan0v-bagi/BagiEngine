#pragma once

#include <EASTL/string_view.h>
#include <type_traits>

/**
 * @file ReflectionMarkers.h
 * @brief Macros for marking classes, fields, and enums for reflection
 *
 * These macros are used by the reflection parser (reflector.py) to identify
 * types that need reflection metadata generation. The generated code provides
 * ReflectionTraits<T> specializations and static class methods.
 *
 * Usage:
 *   struct Player {
 *       BE_CLASS(Player)
 *
 *       BE_REFLECT_FIELD int32_t health = 100;
 *       BE_REFLECT_FIELD float speed = 5.0f;
 *   };
 *
 * Include generated file in .cpp:
 *   #include <Generated/Player.gen.hpp>
 */

namespace BECore {

// Forward declaration for friend access
// Default argument specified here; must not be repeated in TypeTraits.h definition
template<typename T, typename = void>
struct ReflectionTraits;

}  // namespace BECore

// =============================================================================
// BE_CLASS - Main reflection macro for classes/structs
// =============================================================================
// Place inside class body. Declares static reflection methods that are
// defined in the generated .gen.hpp file.
//
// @param ClassName The name of the containing class (required for static methods)
// =============================================================================
#define BE_CLASS(ClassName)                                                     \
public:                                                                          \
    /** @brief Get the type name as string_view */                              \
    static constexpr eastl::string_view GetStaticTypeName();                    \
    /** @brief Get the number of reflected fields */                            \
    static constexpr size_t GetStaticFieldCount();                              \
    /** @brief Iterate over all reflected fields (mutable) */                   \
    template<typename Func>                                                      \
    static constexpr void ForEachFieldStatic(ClassName& obj, Func&& func);      \
    /** @brief Iterate over all reflected fields (const) */                     \
    template<typename Func>                                                      \
    static constexpr void ForEachFieldStatic(const ClassName& obj, Func&& func);\
private:                                                                         \
    template<typename, typename> friend struct ::BECore::ReflectionTraits; \
    public: 

// =============================================================================
// Field and Enum markers
// =============================================================================

// Clang/GCC annotation macros for reflection parser
#if defined(__clang__) || defined(__GNUC__)
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
    #define BE_REFLECT_FIELD /* BE_REFLECT_FIELD */
    #define BE_REFLECT_ENUM  /* BE_REFLECT_ENUM */
#endif

// Legacy macro - kept for compatibility, prefer BE_CLASS(ClassName)
#if defined(__clang__) || defined(__GNUC__)
    #define BE_REFLECT_CLASS [[clang::annotate("reflect_class")]]
#else
    #define BE_REFLECT_CLASS /* BE_REFLECT_CLASS */
#endif
